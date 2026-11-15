/* OS-B13 파일 API와 원자적 저장: write→fsync→rename
 *
 *   Part A  atomic_save(): 임시 파일에 쓰기 → fsync(임시) → close → rename(임시→대상) → fsync(디렉터리).
 *           짧은 쓰기·EINTR 를 견디는 write_all(). 중간에 실패하면 임시 파일을 지우고 원본은 그대로 둔다.
 *   Part B  크래시 주입: 자식 프로세스가 저장하다가 k 번째 파일 연산 직후 SIGKILL 로 죽는다 (k = 1, 2, ...).
 *           부모는 대상 파일이 "정확히 옛 것" 또는 "정확히 새 것" 인지 분류한다. 깨진 변형 3개
 *           (제자리 덮어쓰기 / fsync 없는 rename / 디렉터리 fsync 없음)도 같은 하네스로 돌리고,
 *           kill 로는 못 잡는 것을 "전원 차단 모델"(plfs, 하네스 제공)로 다시 돌린다.
 *   Part C  펌웨어식 설정 레코드: magic·len·payload·CRC32 로 인코딩, 부팅 때 남은 임시 파일 정리 + 검증 로드
 *           (없음/손상 → 기본값). 모든 k 에서 "부팅" 하면 seq 가 옛 것 또는 새 것이다.
 *
 * 저장 코드는 파일 연산을 전부 fsops_t (작은 HAL) 로 부른다. 진짜 구현(realfs)은 POSIX 호출 + 연산 번호 세기
 * + kill 훅, 모델 구현(plfs)은 메모리 안의 "캐시 vs 디스크" 모델이다. 같은 atomic_save 코드가 둘 다에서 돈다.
 *
 * 체크는 매번 성립하는 것만 본다. 시간은 재지 않는다 (fsync 지연 측정은 demo_bench.c).
 * 테스트 파일은 mkdtemp("/tmp/OS-B13_t.XXXXXX") 안에 만들고 끝나면 지운다. 자식은 전부 waitpid, 워치독 있음.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#define _DARWIN_C_SOURCE        /* F_FULLFSYNC, mkdtemp, openat, O_DIRECTORY ... */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ======================= fsops_t: 저장 코드가 쓰는 파일 연산 (하네스 제공) ======================= */
typedef struct fsops fsops_t;
struct fsops {
    int  (*open_trunc)(fsops_t *o, const char *name);            /* O_WRONLY|O_CREAT|O_TRUNC, 0644 → 핸들 */
    long (*write)(fsops_t *o, int h, const void *buf, size_t n);  /* 짧게 쓸 수 있고, -1/EINTR 도 온다 */
    int  (*sync)(fsops_t *o, int h);                              /* fsync (모드에 따라 F_FULLFSYNC) */
    int  (*close)(fsops_t *o, int h);
    int  (*rename)(fsops_t *o, const char *from, const char *to);
    int  (*sync_dir)(fsops_t *o);                                 /* 디렉터리 fsync: 이름(엔트리)을 디스크로 */
    int  (*unlink)(fsops_t *o, const char *name);
};
typedef int (*saver_fn)(fsops_t *o, const char *name, const void *buf, size_t len);

#define TMP_SUFFIX ".tmp"
static int tmp_name(char *out, size_t cap, const char *name) {
    int n = snprintf(out, cap, "%s%s", name, TMP_SUFFIX);
    if (n < 0 || (size_t)n >= cap) { errno = ENAMETOOLONG; return -1; }
    return 0;
}

/* ---------- realfs: 진짜 POSIX 호출 + 연산 번호 + kill 훅 ---------- */
typedef struct {
    fsops_t ops;            /* 첫 멤버여야 한다 (fsops_t* ↔ realfs_t*) */
    int     dirfd;          /* 대상 디렉터리 (openat/renameat/fsync 의 기준) */
    bool    full;           /* true: sync/sync_dir 를 fcntl(F_FULLFSYNC) 로 */
    size_t  max_io;         /* write 한 번에 최대 이만큼 (짧은 쓰기 흉내). 0 = 제한 없음 */
    int     eintr_left;     /* 다음 write 몇 번은 -1/EINTR */
    int     fail_sync;      /* n>0: n 번째 sync 가 -1/EIO (오류 경로 테스트) */
    int     kill_at;        /* 이 번호의 연산이 "성공한 직후" SIGKILL. 0 = 안 죽음 */
    int     nops, nsync;
    char    trace[512];     /* "open write fsync close rename dirsync" */
} realfs_t;

static void rf_step(realfs_t *r, const char *what) {
    r->nops++;
    size_t n = strlen(r->trace);
    if (n + 12 < sizeof r->trace) snprintf(r->trace + n, sizeof r->trace - n, "%s%s", n ? " " : "", what);
    if (r->kill_at == r->nops) kill(getpid(), SIGKILL);   /* 여기서 끝. 커널 페이지 캐시는 그대로 남는다 */
}
static int rf_open_trunc(fsops_t *o, const char *name) {
    realfs_t *r = (realfs_t *)o;
    int fd = openat(r->dirfd, name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) rf_step(r, "open");
    return fd;
}
static long rf_write(fsops_t *o, int h, const void *buf, size_t n) {
    realfs_t *r = (realfs_t *)o;
    if (r->eintr_left > 0) { r->eintr_left--; errno = EINTR; return -1; }
    if (r->max_io && n > r->max_io) n = r->max_io;
    ssize_t w = write(h, buf, n);
    if (w > 0) rf_step(r, "write");
    return (long)w;
}
static int rf_sync(fsops_t *o, int h) {
    realfs_t *r = (realfs_t *)o;
    if (++r->nsync == r->fail_sync) { errno = EIO; return -1; }
    int rc = r->full ? fcntl(h, F_FULLFSYNC) : fsync(h);
    if (rc == 0) rf_step(r, r->full ? "fullfsync" : "fsync");
    return rc;
}
static int rf_close(fsops_t *o, int h) {
    int rc = close(h);
    if (rc == 0) rf_step((realfs_t *)o, "close");
    return rc;
}
static int rf_rename(fsops_t *o, const char *from, const char *to) {
    realfs_t *r = (realfs_t *)o;
    int rc = renameat(r->dirfd, from, r->dirfd, to);
    if (rc == 0) rf_step(r, "rename");
    return rc;
}
static int rf_sync_dir(fsops_t *o) {
    realfs_t *r = (realfs_t *)o;
    int rc = r->full ? fcntl(r->dirfd, F_FULLFSYNC) : fsync(r->dirfd);
    if (rc == 0) rf_step(r, "dirsync");
    return rc;
}
static int rf_unlink(fsops_t *o, const char *name) {
    realfs_t *r = (realfs_t *)o;
    int rc = unlinkat(r->dirfd, name, 0);
    if (rc == 0) rf_step(r, "unlink");
    return rc;
}
static int realfs_open(realfs_t *r, const char *dir) {
    memset(r, 0, sizeof *r);
    r->ops = (fsops_t){ rf_open_trunc, rf_write, rf_sync, rf_close, rf_rename, rf_sync_dir, rf_unlink };
    r->dirfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    return r->dirfd < 0 ? -1 : 0;
}
static void realfs_close(realfs_t *r) { if (r->dirfd >= 0) close(r->dirfd); r->dirfd = -1; }

/* ---------- 테스트 디렉터리와 파일 유틸 (하네스) ---------- */
static char g_dir[64];
static size_t g_max_io;     /* crash_run 의 자식이 쓰는 짧은 쓰기 한도 */

static void path_of(char *out, size_t cap, const char *name) { snprintf(out, cap, "%s/%s", g_dir, name); }
static bool exists(const char *name) {
    char p[160]; struct stat st; path_of(p, sizeof p, name);
    return stat(p, &st) == 0;
}
/* 파일 전체를 읽는다. 0 = 성공(*len), -1 = 실패(errno: ENOENT 없음, EFBIG cap 보다 큼, 그 외) */
static int read_file(const char *name, unsigned char *buf, size_t cap, size_t *len) {
    char p[160]; path_of(p, sizeof p, name);
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t n = 0;
    for (;;) {
        unsigned char extra;
        ssize_t r = n < cap ? read(fd, buf + n, cap - n) : read(fd, &extra, 1);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { int e = errno; close(fd); errno = e; return -1; }
        if (r == 0) break;
        if (n == cap) { close(fd); errno = EFBIG; return -1; }
        n += (size_t)r;
    }
    close(fd);
    *len = n;
    return 0;
}
/* 하네스용 "옛 버전" 깔기: 그냥 쓰고 fsync. 남은 임시 파일도 지운다. */
static void reset_file(const char *name, const void *buf, size_t len) {
    char p[160], t[64];
    path_of(p, sizeof p, name);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) {
        if (write(fd, buf, len) != (ssize_t)len) perror("reset_file write");
        fsync(fd);
        close(fd);
    }
    if (tmp_name(t, sizeof t, name) == 0) { path_of(p, sizeof p, t); unlink(p); }
}
static void remove_all(const char *dir) {
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        char p[200];
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            unlink(p);
        }
        closedir(d);
    }
    rmdir(dir);
}

/* 대상 파일의 상태 */
typedef enum { T_OLD, T_NEW, T_EMPTY, T_TORN, T_MISSING, T_ERR } tstate_t;
static const char *tname(tstate_t t) {
    static const char *n[] = { "OLD", "NEW", "EMPTY", "TORN", "MISS", "ERR" };
    return n[t];
}
/* 바이트열이 옛 것/새 것/빈 것/그 외(찢어짐) 중 무엇인가 */
static tstate_t kind_of(const unsigned char *d, size_t n, const void *oldb, size_t oldn, const void *newb, size_t newn) {
    if (n == 0) return T_EMPTY;
    if (n == oldn && memcmp(d, oldb, n) == 0) return T_OLD;
    if (n == newn && memcmp(d, newb, n) == 0) return T_NEW;
    return T_TORN;
}

/* ======================= Part A — 원자적 저장 ======================= */
/* buf 의 len 바이트를 전부 쓴다. 짧은 쓰기는 이어 쓰고, EINTR 은 다시 시도. 0 / -1(errno). */
static int write_all(fsops_t *o, int h, const void *buf, size_t len) {
    /* TODO: while (len > 0) { n = o->write(o, h, p, len); 짧은 쓰기면 p·len 을 n 만큼 진행.
     *       n < 0 && errno == EINTR → 다시 시도, 그 외 음수 → -1. n == 0 → EIO 로 -1 (무한 루프 방지) } */
    (void)o; (void)h; (void)buf; (void)len;
    errno = ENOSYS;
    return -1;
}

/* name 을 buf 내용으로 원자적으로 바꾼다. 성공(0)이면 새 내용이 디스크까지 갔다(fsync 의미 안에서).
 * 실패(-1)면 대상은 옛 내용 그대로이고 임시 파일은 남기지 않는다 (rename 뒤 dirsync 실패만 예외: 새 내용일 수 있음). */
static int atomic_save(fsops_t *o, const char *name, const void *buf, size_t len) {
    /* TODO: tmp_name() → ① open_trunc(tmp) → ② write_all → ③ sync(h) → ④ close(h) → ⑤ rename(tmp, name)
     *       → ⑥ sync_dir(). ①~⑤ 중 실패하면 핸들을 닫고 tmp 를 unlink 하고 errno 를 보존해 -1.
     *       순서가 테스트된다: "open write... fsync close rename dirsync" */
    (void)o; (void)name; (void)buf; (void)len; (void)write_all;
    errno = ENOSYS;
    return -1;
}

/* ---------- 깨진 변형들 (하네스 제공, 비교용) ---------- */
/* 깨진 1: 제자리 덮어쓰기. truncate 와 write 사이에 죽으면 빈/반쪽 파일 */
static int save_inplace(fsops_t *o, const char *name, const void *buf, size_t len) {
    int h = o->open_trunc(o, name);
    if (h < 0) return -1;
    int rc = write_all(o, h, buf, len);
    if (rc == 0) rc = o->sync(o, h);
    o->close(o, h);
    return rc;
}
/* 깨진 2: rename 은 하지만 fsync 가 하나도 없다 */
static int save_rename_nosync(fsops_t *o, const char *name, const void *buf, size_t len) {
    char tmp[256];
    if (tmp_name(tmp, sizeof tmp, name) < 0) return -1;
    int h = o->open_trunc(o, tmp);
    if (h < 0) return -1;
    if (write_all(o, h, buf, len) < 0) { o->close(o, h); return -1; }
    if (o->close(o, h) < 0) return -1;
    return o->rename(o, tmp, name);
}
/* 깨진 3: 파일 fsync + rename, 그러나 디렉터리 fsync 가 없다 (원자성은 OK, 내구성이 문제) */
static int save_no_dirsync(fsops_t *o, const char *name, const void *buf, size_t len) {
    char tmp[256];
    if (tmp_name(tmp, sizeof tmp, name) < 0) return -1;
    int h = o->open_trunc(o, tmp);
    if (h < 0) return -1;
    if (write_all(o, h, buf, len) < 0 || o->sync(o, h) < 0) { o->close(o, h); return -1; }
    if (o->close(o, h) < 0) return -1;
    return o->rename(o, tmp, name);
}

/* ======================= Part B — 크래시 주입 하네스 ======================= */
typedef enum { RUN_KILLED, RUN_DONE, RUN_ERROR, RUN_TIMEOUT, RUN_OTHER } run_t;

/* (하네스) 자식 프로세스 본체: realfs 를 열고 kill_at 을 걸고 save 를 부른 뒤 _exit.
 * kill_at 번째 연산 직후 SIGKILL 로 죽거나, 끝까지 가면 0(성공)/1(실패)로 끝난다. */
static _Noreturn void child_save(saver_fn save, const char *name, const void *buf, size_t len, int kill_at) {
    alarm(5);                                       /* 이중 안전장치: 무슨 일이 있어도 5 초 뒤엔 죽는다 */
    realfs_t r;
    if (realfs_open(&r, g_dir) < 0) _exit(2);
    r.kill_at = kill_at;
    r.max_io = g_max_io;
    int rc = save(&r.ops, name, buf, len);
    _exit(rc == 0 ? 0 : 1);                         /* exit() 아님: 부모에게서 복사된 stdio 버퍼를 또 쓰지 않게 */
}
/* (하네스) pid 를 최대 ms 동안 기다린다. 시간 초과면 SIGKILL 후 거둬들이고 false. 좀비를 남기지 않는다. */
static bool wait_child(pid_t pid, int ms, int *status) {
    for (int t = 0;; t++) {
        pid_t w = waitpid(pid, status, WNOHANG);
        if (w == pid) return true;
        if (w < 0 && errno != EINTR) return false;
        if (t >= ms * 4) break;
        struct timespec ts = { 0, 250000 };         /* 0.25 ms 간격으로 확인 */
        nanosleep(&ts, NULL);
    }
    kill(pid, SIGKILL);
    while (waitpid(pid, status, 0) < 0 && errno == EINTR) { }
    return false;
}

/* 자식에서 save 를 돌리고, kill_at 번째 파일 연산 직후 죽게 한다. 어떻게 끝났는지 돌려준다.
 *   RUN_KILLED  : SIGKILL 로 죽었다 (훅이 쏨)
 *   RUN_DONE    : 끝까지 가서 exit 0     RUN_ERROR : exit 0 이 아닌 값
 *   RUN_TIMEOUT : timeout_ms 안에 안 끝나 워치독이 죽였다     RUN_OTHER : fork 실패 등 */
static run_t crash_run(saver_fn save, const char *name, const void *buf, size_t len, int kill_at, int timeout_ms) {
    /* TODO: fflush(stdout) → fork(). 자식: child_save(save, name, buf, len, kill_at) (돌아오지 않음).
     *       부모: wait_child(pid, timeout_ms, &st) 가 false 면 RUN_TIMEOUT.
     *       WIFSIGNALED && WTERMSIG == SIGKILL → RUN_KILLED, WIFEXITED → 0 이면 RUN_DONE 아니면 RUN_ERROR. */
    (void)save; (void)name; (void)buf; (void)len; (void)kill_at; (void)timeout_ms;
    (void)child_save; (void)wait_child;
    return RUN_OTHER;
}

/* 대상 파일을 읽어 분류한다: 없음 → T_MISSING, 읽기 실패 → T_ERR, 그 외 kind_of() */
static tstate_t classify(const char *name, const void *oldb, size_t oldn, const void *newb, size_t newn) {
    /* TODO: read_file(name, buf, sizeof buf, &n). 실패하면 errno 로: ENOENT → T_MISSING, EFBIG → T_TORN,
     *       그 외 T_ERR. 성공하면 kind_of(buf, n, oldb, oldn, newb, newn). */
    (void)name; (void)oldb; (void)oldn; (void)newb; (void)newn;
    return T_ERR;
}

/* ---------- kill 스윕 (하네스): k = 1, 2, ... 마다 옛 버전을 깔고 crash_run → classify ---------- */
#define MAXK 24
typedef struct {
    tstate_t st[MAXK]; run_t run[MAXK]; bool tmp_left[MAXK];
    int n;                  /* 돌린 k 개수 (마지막은 DONE 이어야 정상) */
} sweep_t;
static void kill_sweep(saver_fn save, const char *name, const void *oldb, size_t oldn,
                       const void *newb, size_t newn, sweep_t *s) {
    memset(s, 0, sizeof *s);
    char t[64];
    tmp_name(t, sizeof t, name);
    for (int k = 1; k <= MAXK; k++) {
        reset_file(name, oldb, oldn);
        run_t r = crash_run(save, name, newb, newn, k, 2000);
        s->run[s->n] = r;
        s->st[s->n] = classify(name, oldb, oldn, newb, newn);
        s->tmp_left[s->n] = exists(t);
        s->n++;
        if (r != RUN_KILLED) break;                 /* 끝까지 갔거나(DONE) 이상하면 그만 */
    }
}
static void print_sweep(const char *label, const sweep_t *s) {
    printf("  %-18s", label);
    for (int i = 0; i < s->n; i++)
        printf(" %s%s", s->run[i] == RUN_KILLED ? "" : (s->run[i] == RUN_DONE ? "done:" : "?:"), tname(s->st[i]));
    printf("\n");
}
static bool sweep_only_old_new(const sweep_t *s) {
    if (s->n == 0) return false;
    for (int i = 0; i < s->n; i++) if (s->st[i] != T_OLD && s->st[i] != T_NEW) return false;
    return true;
}
static bool sweep_saw(const sweep_t *s, tstate_t t) {
    for (int i = 0; i < s->n; i++) if (s->st[i] == t) return true;
    return false;
}

/* ---------- plfs: 전원 차단 모델 (하네스 제공) ----------
 * 커널처럼 "캐시(메모리)" 와 "디스크(내구)" 를 따로 든다.
 *  - 파일 내용: write/truncate 는 캐시만 바꾼다. sync(h) 를 해야 그 파일 내용이 디스크로 간다.
 *  - 이름(디렉터리): 생성·rename·unlink 는 메타데이터 로그에 쌓인다. sync_dir 를 해야 디스크로 간다.
 *    (POSIX 는 파일 fsync 가 그 파일의 디렉터리 엔트리까지 내린다고 약속하지 않는다 → 모델은 비관적으로)
 * 전원 차단 = 디스크에 남을 수 있는 모든 조합을 나열한다:
 *    메타데이터 로그는 앞에서부터 아무 길이(저널 FS 처럼 순서는 지킴) × 더러운 파일마다 {디스크 것, 캐시 것}.
 * 단순화: 파일 내용은 통째로 둘 중 하나만 고른다(일부 페이지만 내려가는 경우는 없음) → 실제보다 관대하다. */
#define PL_INO 6
#define PL_LOG 8
#define PL_DENT 8
#define PL_CAP 4096
typedef struct { char name[32]; int ino; } pl_dent_t;
typedef struct { int kind; char a[32], b[32]; int ino; } pl_meta_t;   /* kind 0 create, 1 rename, 2 unlink */
typedef struct {
    fsops_t ops;
    unsigned char cache[PL_INO][PL_CAP], disk[PL_INO][PL_CAP];
    size_t clen[PL_INO], dlen[PL_INO], pos[PL_INO];
    bool dirty[PL_INO];
    int ninode;
    pl_dent_t ddir[PL_DENT]; int nddir;             /* 디스크에 있는 디렉터리 */
    pl_meta_t log[PL_LOG]; int nlog;                /* 아직 디스크에 없는 이름 연산 */
    int crash_at, nops; bool dead;
    char trace[256];
} plfs_t;

static int pl_view(const plfs_t *p, int upto, pl_dent_t *v) {
    int n = p->nddir;
    memcpy(v, p->ddir, sizeof(pl_dent_t) * (size_t)n);
    for (int i = 0; i < upto; i++) {
        const pl_meta_t *m = &p->log[i];
        int a = -1, b = -1;
        for (int j = 0; j < n; j++) { if (!strcmp(v[j].name, m->a)) a = j; if (!strcmp(v[j].name, m->b)) b = j; }
        if (m->kind == 0 && a < 0 && n < PL_DENT) { snprintf(v[n].name, sizeof v[n].name, "%s", m->a); v[n++].ino = m->ino; }
        if (m->kind == 1 && a >= 0) {
            if (b >= 0) { v[b] = v[--n]; if (a == n) a = b; }
            snprintf(v[a].name, sizeof v[a].name, "%s", m->b);
        }
        if (m->kind == 2 && a >= 0) v[a] = v[--n];
    }
    return n;
}
static int pl_lookup(const pl_dent_t *v, int n, const char *name) {
    for (int i = 0; i < n; i++) if (!strcmp(v[i].name, name)) return v[i].ino;
    return -1;
}
static bool pl_alive(plfs_t *p) { if (p->dead) errno = EIO; return !p->dead; }
static void pl_step(plfs_t *p, const char *what) {
    p->nops++;
    size_t n = strlen(p->trace);
    if (n + 12 < sizeof p->trace) snprintf(p->trace + n, sizeof p->trace - n, "%s%s", n ? " " : "", what);
    if (p->nops == p->crash_at) p->dead = true;     /* 이 연산까지 하고 전원이 나간다 */
}
static bool pl_log(plfs_t *p, int kind, const char *a, const char *b, int ino) {
    if (p->nlog == PL_LOG) { errno = ENOSPC; return false; }
    pl_meta_t *m = &p->log[p->nlog++];
    m->kind = kind; m->ino = ino;
    snprintf(m->a, sizeof m->a, "%s", a);
    snprintf(m->b, sizeof m->b, "%s", b ? b : "");
    return true;
}
static int pl_open_trunc(fsops_t *o, const char *name) {
    plfs_t *p = (plfs_t *)o;
    if (!pl_alive(p)) return -1;
    pl_dent_t v[PL_DENT]; int n = pl_view(p, p->nlog, v);
    int ino = pl_lookup(v, n, name);
    if (ino < 0) {
        if (p->ninode == PL_INO) { errno = ENOSPC; return -1; }
        ino = p->ninode++;
        if (!pl_log(p, 0, name, NULL, ino)) return -1;
    }
    p->clen[ino] = 0; p->pos[ino] = 0; p->dirty[ino] = true;
    pl_step(p, "open");
    return ino;
}
static long pl_write(fsops_t *o, int h, const void *buf, size_t n) {
    plfs_t *p = (plfs_t *)o;
    if (!pl_alive(p)) return -1;
    if (n > PL_CAP - p->pos[h]) n = PL_CAP - p->pos[h];
    if (n == 0) { errno = ENOSPC; return -1; }
    memcpy(p->cache[h] + p->pos[h], buf, n);
    p->pos[h] += n;
    if (p->pos[h] > p->clen[h]) p->clen[h] = p->pos[h];
    p->dirty[h] = true;
    pl_step(p, "write");
    return (long)n;
}
static int pl_sync(fsops_t *o, int h) {
    plfs_t *p = (plfs_t *)o;
    if (!pl_alive(p)) return -1;
    memcpy(p->disk[h], p->cache[h], p->clen[h]); p->dlen[h] = p->clen[h]; p->dirty[h] = false;
    pl_step(p, "fsync");
    return 0;
}
static int pl_close(fsops_t *o, int h) {
    plfs_t *p = (plfs_t *)o; (void)h;
    if (!pl_alive(p)) return -1;
    pl_step(p, "close");
    return 0;
}
static int pl_rename(fsops_t *o, const char *from, const char *to) {
    plfs_t *p = (plfs_t *)o;
    if (!pl_alive(p)) return -1;
    pl_dent_t v[PL_DENT]; int n = pl_view(p, p->nlog, v);
    if (pl_lookup(v, n, from) < 0) { errno = ENOENT; return -1; }
    if (!pl_log(p, 1, from, to, -1)) return -1;
    pl_step(p, "rename");
    return 0;
}
static int pl_sync_dir(fsops_t *o) {
    plfs_t *p = (plfs_t *)o;
    if (!pl_alive(p)) return -1;
    pl_dent_t v[PL_DENT]; int n = pl_view(p, p->nlog, v);
    memcpy(p->ddir, v, sizeof v); p->nddir = n; p->nlog = 0;
    pl_step(p, "dirsync");
    return 0;
}
static int pl_unlink(fsops_t *o, const char *name) {
    plfs_t *p = (plfs_t *)o;
    if (!pl_alive(p)) return -1;
    pl_dent_t v[PL_DENT]; int n = pl_view(p, p->nlog, v);
    if (pl_lookup(v, n, name) < 0) { errno = ENOENT; return -1; }
    if (!pl_log(p, 2, name, NULL, -1)) return -1;
    pl_step(p, "unlink");
    return 0;
}
static void plfs_init(plfs_t *p, const char *name, const void *oldb, size_t oldn) {
    memset(p, 0, sizeof *p);
    p->ops = (fsops_t){ pl_open_trunc, pl_write, pl_sync, pl_close, pl_rename, pl_sync_dir, pl_unlink };
    p->ninode = 1;                                  /* inode 0 = 옛 내용, 디스크에도 있음 */
    memcpy(p->cache[0], oldb, oldn); memcpy(p->disk[0], oldb, oldn);
    p->clen[0] = p->dlen[0] = oldn;
    snprintf(p->ddir[0].name, sizeof p->ddir[0].name, "%s", name); p->ddir[0].ino = 0; p->nddir = 1;
}
/* 지금 전원이 나가면 name 이 될 수 있는 상태를 전부 센다 */
typedef struct { int cnt[6]; int total; } pl_tally_t;
static void pl_crash_states(const plfs_t *p, const char *name, const void *oldb, size_t oldn,
                            const void *newb, size_t newn, pl_tally_t *t) {
    int dirty_ino[PL_INO], nd = 0;
    for (int i = 0; i < p->ninode; i++) if (p->dirty[i]) dirty_ino[nd++] = i;
    for (int pre = 0; pre <= p->nlog; pre++) {
        pl_dent_t v[PL_DENT]; int n = pl_view(p, pre, v);
        int ino = pl_lookup(v, n, name);
        for (unsigned mask = 0; mask < (1u << nd); mask++) {
            tstate_t s = T_MISSING;
            if (ino >= 0) {
                bool use_cache = false;
                for (int j = 0; j < nd; j++) if (dirty_ino[j] == ino && ((mask >> j) & 1u)) use_cache = true;
                s = use_cache ? kind_of(p->cache[ino], p->clen[ino], oldb, oldn, newb, newn)
                              : kind_of(p->disk[ino], p->dlen[ino], oldb, oldn, newb, newn);
            }
            t->cnt[s]++; t->total++;
        }
    }
}
/* 모델 스윕: k = 1.. 마다 "k 번째 연산 직후 전원 차단" 의 모든 상태 + 저장이 성공한 뒤의 모든 상태 */
typedef struct { pl_tally_t during, after; int nops; bool after_ok; char trace[256]; } pl_report_t;
static void pl_sweep(saver_fn save, const char *name, const void *oldb, size_t oldn,
                     const void *newb, size_t newn, pl_report_t *r) {
    static plfs_t p;
    memset(r, 0, sizeof *r);
    plfs_init(&p, name, oldb, oldn);                /* 먼저 한 번 끝까지: 연산 개수와 "성공 뒤" 상태 */
    r->after_ok = save(&p.ops, name, newb, newn) == 0;
    r->nops = p.nops;
    snprintf(r->trace, sizeof r->trace, "%s", p.trace);
    pl_crash_states(&p, name, oldb, oldn, newb, newn, &r->after);
    for (int k = 1; k <= r->nops; k++) {
        plfs_init(&p, name, oldb, oldn);
        p.crash_at = k;
        (void)save(&p.ops, name, newb, newn);       /* k 뒤로는 전부 EIO → 저장 함수가 알아서 그만둔다 */
        pl_crash_states(&p, name, oldb, oldn, newb, newn, &r->during);
    }
}
static void print_pl(const char *label, const pl_report_t *r) {
    printf("  %-18s during:", label);
    for (int s = 0; s < 5; s++) if (r->during.cnt[s]) printf(" %s=%d", tname((tstate_t)s), r->during.cnt[s]);
    printf("  | after success:");
    for (int s = 0; s < 5; s++) if (r->after.cnt[s]) printf(" %s=%d", tname((tstate_t)s), r->after.cnt[s]);
    printf("\n");
}

/* ======================= Part C — 펌웨어식 설정 레코드 ======================= */
/* 레코드 (리틀 엔디언 고정):  [0] magic "CFG1"  [4] len=28  [8] payload 28B  [36] crc32(앞 36 바이트)
 * payload: seq u32 | gain_mdb i32 | rate_hz u32 | name[16] (NUL 포함) */
#define CFG_MAGIC   0x31474643u    /* 'C' 'F' 'G' '1' 를 리틀 엔디언으로 읽은 값 */
#define CFG_PAYLOAD 28u
#define CFG_REC     (8u + CFG_PAYLOAD + 4u)
#define CFG_NAME    "config.bin"
typedef struct { uint32_t seq; int32_t gain_mdb; uint32_t rate_hz; char name[16]; } config_t;
typedef enum { CFG_LOADED, CFG_MISSING, CFG_CORRUPT } cfg_src_t;
static const config_t CFG_DEFAULT = { 0, 0, 100, "default" };

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t crc32_ieee(const void *data, size_t n) {    /* 반사형, 다항식 0xEDB88320 (zlib/Ethernet 과 같음) */
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}

/* config 를 레코드로. 성공하면 CFG_REC, cap 이 모자라면 0. */
static size_t config_encode(const config_t *c, uint8_t *out, size_t cap) {
    /* TODO: cap < CFG_REC 면 0. put32 로 magic, len, seq, gain, rate 를 쓰고 name 16 바이트를 복사,
     *       마지막에 앞 36 바이트의 crc32_ieee 를 [36] 에. CFG_REC 반환. */
    (void)c; (void)out; (void)cap;
    return 0;
}
/* 레코드 검증 + 해독. 길이·magic·len·CRC·name 의 NUL 중 하나라도 틀리면 false (*out 은 건드리지 않음). */
static bool config_decode(const uint8_t *in, size_t n, config_t *out) {
    /* TODO: n == CFG_REC, magic, len == CFG_PAYLOAD, CRC, name 안에 NUL — 전부 통과해야 *out 을 채우고 true. */
    (void)in; (void)n; (void)out;
    return false;
}
/* 부팅 시: 남은 임시 파일을 지우고(*removed_tmp), CFG_NAME 을 읽어 검증한다.
 * 없으면 CFG_MISSING, 검증 실패면 CFG_CORRUPT — 둘 다 *out = CFG_DEFAULT. */
static cfg_src_t config_boot(config_t *out, bool *removed_tmp) {
    /* TODO: ① CFG_NAME TMP_SUFFIX 가 있으면 unlink 하고 *removed_tmp = true.
     *       ② read_file(CFG_NAME, buf, CFG_REC + 1, &n): 실패 → ENOENT 면 CFG_MISSING, 아니면 CFG_CORRUPT.
     *       ③ config_decode 실패 → CFG_CORRUPT. ②③ 실패 때는 *out = CFG_DEFAULT. */
    (void)crc32_ieee; (void)get32; (void)put32;
    *removed_tmp = false;
    *out = CFG_DEFAULT;
    return CFG_MISSING;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int saver_fail(fsops_t *o, const char *name, const void *buf, size_t len) {
    (void)o; (void)name; (void)buf; (void)len; errno = EIO; return -1;
}
static int saver_hang(fsops_t *o, const char *name, const void *buf, size_t len) {
    (void)o; (void)name; (void)buf; (void)len;
    for (;;) pause();                               /* 영원히 안 끝나는 저장: 워치독이 잡아야 한다 */
}
static bool file_is(const char *name, const void *want, size_t n) {
    unsigned char buf[8192]; size_t got = 0;
    return read_file(name, buf, sizeof buf, &got) == 0 && got == n && memcmp(buf, want, n) == 0;
}

int main(void) {
    snprintf(g_dir, sizeof g_dir, "/tmp/OS-B13_t.XXXXXX");
    if (!mkdtemp(g_dir)) { perror("mkdtemp"); return 1; }

    static unsigned char OLDB[2500], NEWB[3000], big[1000];
    for (size_t i = 0; i < sizeof OLDB; i++) OLDB[i] = (unsigned char)('a' + i % 26);
    for (size_t i = 0; i < sizeof NEWB; i++) NEWB[i] = (unsigned char)('A' + i % 26);
    for (size_t i = 0; i < sizeof big; i++) big[i] = (unsigned char)(i * 7u);
    const char *AN = "app.conf";
    realfs_t r;

    /* ---------------- Part A: atomic_save 기본 ---------------- */
    if (realfs_open(&r, g_dir) < 0) { perror("open dir"); return 1; }
    CHECK(atomic_save(&r.ops, AN, "v1\n", 3) == 0);
    CHECK(file_is(AN, "v1\n", 3) && !exists("app.conf.tmp"));                  /* 새로 만들기, 임시 파일 없음 */
    printf("Part A ops: %s\n", r.trace);
    CHECK(strcmp(r.trace, "open write fsync close rename dirsync") == 0);      /* 순서가 곧 정답 */
    realfs_close(&r);

    reset_file(AN, OLDB, sizeof OLDB);                                         /* 긴 옛 것 → 짧은 새 것 */
    realfs_open(&r, g_dir);
    CHECK(atomic_save(&r.ops, AN, "short", 5) == 0 && file_is(AN, "short", 5));
    realfs_close(&r);

    realfs_open(&r, g_dir); r.max_io = 7;                                      /* 짧은 쓰기: 7 바이트씩 */
    CHECK(atomic_save(&r.ops, AN, big, sizeof big) == 0 && file_is(AN, big, sizeof big));
    CHECK(r.nops == 1 + (int)((sizeof big + 6) / 7) + 4);                      /* open + 143 write + 4 */
    realfs_close(&r);

    realfs_open(&r, g_dir); r.eintr_left = 3;                                  /* EINTR 3 번 */
    CHECK(atomic_save(&r.ops, AN, "eintr", 5) == 0 && file_is(AN, "eintr", 5));
    realfs_close(&r);

    reset_file(AN, "old", 3);                                                  /* 더 긴 찌꺼기 임시 파일이 남아 있어도 */
    { char p[160]; path_of(p, sizeof p, "app.conf.tmp");
      int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd >= 0) { if (write(fd, OLDB, sizeof OLDB) < 0) perror("write"); close(fd); } }
    realfs_open(&r, g_dir);
    CHECK(atomic_save(&r.ops, AN, "new", 3) == 0 && file_is(AN, "new", 3));
    realfs_close(&r);

    reset_file(AN, "old", 3);                                                  /* fsync 가 EIO 면 */
    realfs_open(&r, g_dir); r.fail_sync = 1; errno = 0;
    int rc = atomic_save(&r.ops, AN, "new", 3);
    int e = errno;
    CHECK(rc == -1 && e == EIO);
    CHECK(file_is(AN, "old", 3) && !exists("app.conf.tmp"));                   /* 원본 그대로, 임시 파일 정리 */
    realfs_close(&r);

    realfs_open(&r, g_dir); errno = 0;                                         /* 없는 디렉터리 안 */
    rc = atomic_save(&r.ops, "nope/app.conf", "x", 1);
    e = errno;
    CHECK(rc == -1 && e == ENOENT);
    realfs_close(&r);

    realfs_open(&r, g_dir); r.full = true;                                     /* F_FULLFSYNC 모드 */
    CHECK(atomic_save(&r.ops, AN, "full", 4) == 0 && file_is(AN, "full", 4));
    printf("Part A ops (full): %s\n", r.trace);
    realfs_close(&r);

    /* ---------------- Part B: kill 스윕 ---------------- */
    printf("Part B kill sweep: SIGKILL right after file op k = 1, 2, ... (old 2500 B -> new 3000 B, write <= 1024 B)\n");
    g_max_io = 1024;
    sweep_t sa, si, sn, sd;
    kill_sweep(atomic_save, AN, OLDB, sizeof OLDB, NEWB, sizeof NEWB, &sa);
    kill_sweep(save_inplace, AN, OLDB, sizeof OLDB, NEWB, sizeof NEWB, &si);
    kill_sweep(save_rename_nosync, AN, OLDB, sizeof OLDB, NEWB, sizeof NEWB, &sn);
    kill_sweep(save_no_dirsync, AN, OLDB, sizeof OLDB, NEWB, sizeof NEWB, &sd);
    realfs_open(&r, g_dir); r.max_io = 1024;
    (void)atomic_save(&r.ops, AN, NEWB, sizeof NEWB);
    printf("  atomic_save ops:   %s\n", r.trace);
    realfs_close(&r);
    print_sweep("atomic_save", &sa);
    print_sweep("inplace", &si);
    print_sweep("rename_nosync", &sn);
    print_sweep("no_dirsync", &sd);

    CHECK(sa.n == 9 && sa.run[8] == RUN_DONE);                                 /* 8 연산 + 끝까지 */
    bool all_killed = sa.n > 1;
    for (int i = 0; i + 1 < sa.n; i++) all_killed = all_killed && sa.run[i] == RUN_KILLED;
    CHECK(all_killed);
    CHECK(sweep_only_old_new(&sa));                                            /* 핵심: 절대 반쪽·없음이 아님 */
    CHECK(sa.n == 9 && sa.st[5] == T_OLD && sa.st[6] == T_NEW);                /* rename(7번째) 가 경계 */
    CHECK(sa.n == 9 && sa.tmp_left[0] && !sa.tmp_left[8]);                     /* 일찍 죽으면 임시 파일이 남는다 → Part C */
    CHECK(sweep_saw(&si, T_EMPTY) && sweep_saw(&si, T_TORN));                  /* 제자리 덮어쓰기: 잡힌다 */
    CHECK(sweep_only_old_new(&sn) && sweep_only_old_new(&sd));                 /* fsync 빠뜨린 건 kill 로 안 잡힌다! */

    /* 하네스 자체 점검: 실패하는 저장, 영원히 안 끝나는 저장 */
    CHECK(crash_run(saver_fail, AN, "x", 1, 0, 2000) == RUN_ERROR);
    CHECK(crash_run(saver_hang, AN, "x", 1, 0, 150) == RUN_TIMEOUT);
    errno = 0;
    CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);                /* 좀비·남은 자식 없음 */

    /* 전원 차단 모델: 같은 저장 함수를 plfs 위에서 */
    printf("Part B power-loss model (plfs): every surviving state of '%s'\n", AN);
    pl_report_t pa, pi, pn, pd;
    pl_sweep(atomic_save, AN, OLDB, sizeof OLDB, NEWB, sizeof NEWB, &pa);
    pl_sweep(save_inplace, AN, OLDB, sizeof OLDB, NEWB, sizeof NEWB, &pi);
    pl_sweep(save_rename_nosync, AN, OLDB, sizeof OLDB, NEWB, sizeof NEWB, &pn);
    pl_sweep(save_no_dirsync, AN, OLDB, sizeof OLDB, NEWB, sizeof NEWB, &pd);
    printf("  model ops: %s\n", pa.trace);
    print_pl("atomic_save", &pa);
    print_pl("inplace", &pi);
    print_pl("rename_nosync", &pn);
    print_pl("no_dirsync", &pd);
    CHECK(pa.after_ok && pa.nops == 6);
    CHECK(pa.during.total > 0 && pa.during.cnt[T_OLD] + pa.during.cnt[T_NEW] == pa.during.total);
    CHECK(pa.after.total > 0 && pa.after.cnt[T_NEW] == pa.after.total);        /* 성공 뒤엔 무조건 새 것 */
    CHECK(pn.after.cnt[T_EMPTY] > 0);                                          /* 이름은 갔는데 내용은 안 감 */
    CHECK(pd.during.cnt[T_OLD] + pd.during.cnt[T_NEW] == pd.during.total && pd.after.cnt[T_OLD] > 0); /* 원자적이지만 성공 후 잃음 */
    CHECK(pi.during.cnt[T_EMPTY] + pi.during.cnt[T_TORN] > 0);

    /* ---------------- Part C: 설정 레코드 ---------------- */
    config_t c_old = { 7, -1500, 1000, "imu-1kHz" }, c_new = { 8, -1200, 2000, "imu-2kHz" }, got;
    uint8_t rec_old[64], rec_new[64], bad[64];
    printf("Part C crc32(\"123456789\") = 0x%08X\n", crc32_ieee("123456789", 9));
    CHECK(crc32_ieee("123456789", 9) == 0xCBF43926u);                         /* 하네스 CRC 표준 값 */
    size_t ln = config_encode(&c_old, rec_old, sizeof rec_old);
    CHECK(ln == CFG_REC && memcmp(rec_old, "CFG1", 4) == 0 && rec_old[4] == CFG_PAYLOAD);
    CHECK(config_encode(&c_old, rec_old, CFG_REC - 1) == 0);
    memset(&got, 0, sizeof got);
    CHECK(config_decode(rec_old, CFG_REC, &got) && got.seq == 7 && got.gain_mdb == -1500 &&
          got.rate_hz == 1000 && strcmp(got.name, "imu-1kHz") == 0);
    (void)config_encode(&c_new, rec_new, sizeof rec_new);
    memcpy(bad, rec_old, CFG_REC); bad[13] ^= 0x01;
    CHECK(!config_decode(bad, CFG_REC, &got));                                 /* 비트 1 개 뒤집힘 */
    CHECK(!config_decode(rec_old, CFG_REC - 1, &got) && !config_decode(rec_old, 0, &got)); /* 잘림 */
    memcpy(bad, rec_old, CFG_REC); bad[0] = 'X'; put32(bad + 36, crc32_ieee(bad, 36));
    CHECK(!config_decode(bad, CFG_REC, &got));                                 /* CRC 는 맞지만 magic 이 틀림 */
    memcpy(bad, rec_old, CFG_REC); put32(bad + 4, 0xFFFFFFF0u); put32(bad + 36, crc32_ieee(bad, 36));
    CHECK(!config_decode(bad, CFG_REC, &got));                                 /* 말이 안 되는 len */
    memcpy(bad, rec_old, CFG_REC); memset(bad + 20, 'A', 16); put32(bad + 36, crc32_ieee(bad, 36));
    CHECK(!config_decode(bad, CFG_REC, &got));                                 /* name 에 NUL 없음 */

    bool rm = true;
    { char p[160]; path_of(p, sizeof p, CFG_NAME); unlink(p); }
    CHECK(config_boot(&got, &rm) == CFG_MISSING && got.seq == 0 && got.rate_hz == 100 && !rm);
    reset_file(CFG_NAME, rec_old, CFG_REC);
    { char p[160]; path_of(p, sizeof p, CFG_NAME TMP_SUFFIX);
      int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd >= 0) { if (write(fd, rec_new, 10) < 0) perror("write"); close(fd); } }
    CHECK(config_boot(&got, &rm) == CFG_LOADED && got.seq == 7 && rm && !exists(CFG_NAME TMP_SUFFIX));

    g_max_io = 16;                                                             /* 40 B 레코드 = write 3 번 */
    printf("Part C boot after crash at op k (save seq 7 -> 8):\n");
    for (int pass = 0; pass < 2; pass++) {
        saver_fn sv = pass == 0 ? atomic_save : save_inplace;
        int loaded_ok = 0, corrupt = 0, wrong = 0, runs = 0, tmp_after = 0, removed = 0;
        bool done = false;
        printf("  %-12s", pass == 0 ? "atomic_save" : "inplace");
        for (int k = 1; k <= MAXK && !done; k++) {
            reset_file(CFG_NAME, rec_old, CFG_REC);
            run_t rr = crash_run(sv, CFG_NAME, rec_new, CFG_REC, k, 2000);
            done = rr != RUN_KILLED;
            cfg_src_t src = config_boot(&got, &rm);
            runs++; removed += rm; tmp_after += exists(CFG_NAME TMP_SUFFIX);
            if (src == CFG_LOADED && (got.seq == 7 || got.seq == 8)) loaded_ok++;
            else if (src == CFG_CORRUPT && got.seq == 0) corrupt++;
            else wrong++;
            printf(" %s", src == CFG_LOADED ? (got.seq == 7 ? "seq7" : got.seq == 8 ? "seq8" : "seq?") :
                          src == CFG_CORRUPT ? "CORRUPT" : "MISSING");
        }
        printf("  (tmp removed at boot: %d)\n", removed);
        if (pass == 0) {
            CHECK(done && runs == 9 && loaded_ok == runs && wrong == 0);      /* 언제 죽어도 seq 7 또는 8 */
            CHECK(removed >= 5 && tmp_after == 0);                             /* 남은 임시 파일은 부팅 때 정리 */
            CHECK(got.seq == 8);                                               /* 마지막(끝까지 간) 저장은 새 것 */
        } else {
            CHECK(done && wrong == 0 && corrupt > 0);                          /* 반쪽은 CRC 가 잡는다 → 기본값 */
        }
    }

    remove_all(g_dir);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
