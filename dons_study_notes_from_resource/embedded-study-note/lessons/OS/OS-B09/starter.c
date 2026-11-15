/* OS-B09 mmap: 파일 매핑, 익명 매핑, MAP_SHARED vs PRIVATE
 *
 *   Part A  파일 매핑: MAP_SHARED / MAP_PRIVATE 로 쓰면 파일·다른 매핑·write(2) 와 어떻게 보이나 → 표
 *   Part B  익명 매핑 + fork 후 공유 여부 표, 파일 끝(EOF) 너머 접근 → SIGBUS, munmap 뒤 → SIGSEGV,
 *           msync 오류 규칙. 시그널은 자식 프로세스 안에서 일어나게 하고 부모가 결과만 받는다.
 *   Part C  파일 복사 두 가지: read/write 루프 vs mmap + memcpy (정확성만 체크, 속도는 demo_bench.c)
 *
 * 체크는 "이 맥에서 매번 성립" 하는 것만 본다. 시간은 재지 않는다.
 * 자식이 멈춰도 wait_child() 가 2초 뒤 SIGKILL 로 정리하고, main 의 alarm(60) 이 최후의 안전장치다.
 * 임시 파일은 mkdtemp 로 만든 /tmp/OS-B09_XXXXXX 안에만 만들고 끝에서 전부 지운다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _DARWIN_C_SOURCE        /* macOS: MAP_ANON, mkdtemp 등 */
#define _DEFAULT_SOURCE         /* Linux glibc: 같은 것들 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static long PG;                 /* sysconf(_SC_PAGESIZE): Apple Silicon 16384, 보통의 Linux/x86 4096 */

/* ---------------- 하네스 제공 도우미 ---------------- */
static char g_dir[64];          /* mkdtemp 결과: /tmp/OS-B09_XXXXXX */

static const char *tmp_path(const char *name) {     /* 돌려쓰는 버퍼 8개 — 한 식에서 여러 번 불러도 됨 */
    static char buf[8][128];
    static int i;
    char *p = buf[i++ & 7];
    snprintf(p, sizeof buf[0], "%s/%s", g_dir, name);
    return p;
}

/* path 를 n 바이트 fill 로 채워 새로 만들고 O_RDWR fd 를 돌려준다. */
static int make_file(const char *path, size_t n, char fill) {
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    char blk[4096];
    memset(blk, fill, sizeof blk);
    for (size_t off = 0; off < n; ) {
        size_t k = n - off < sizeof blk ? n - off : sizeof blk;
        if (pwrite(fd, blk, k, (off_t)off) != (ssize_t)k) { close(fd); return -1; }
        off += k;
    }
    return fd;
}

/* 파일의 off 위치 1바이트 (read 경로로 본 값). EOF 면 -1. */
static int file_byte(int fd, off_t off) {
    unsigned char c;
    return pread(fd, &c, 1, off) == 1 ? c : -1;
}

static long long file_size(int fd) {
    struct stat st;
    return fstat(fd, &st) == 0 ? (long long)st.st_size : -1;
}

/* 자식을 최대 ms 밀리초 기다린다. 끝났으면 waitpid status, 시간 초과면 SIGKILL 후 -1. */
static int wait_child(pid_t pid, int ms) {
    int st;
    for (int t = 0; t < ms; t++) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return st;
        if (r < 0 && errno != EINTR) return -1;
        struct timespec one = {0, 1000000};
        nanosleep(&one, NULL);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &st, 0);
    return -1;
}

/* 자식 프로세스에서만 부른다: SIGBUS/SIGSEGV 를 잡아 종료 코드 FAULT_EXIT_BASE + 시그널 번호로 끝낸다.
 * 잡지 않고 죽게 두면 macOS 가 ~/Library/Logs/DiagnosticReports 에 크래시 리포트(.ips)를 남긴다. */
#define FAULT_EXIT_BASE 100
static void on_fault(int sig) { _exit(FAULT_EXIT_BASE + sig); }   /* _exit 는 async-signal-safe */
static void child_catch_faults(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fault;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    alarm(2);                    /* 자식 쪽 워치독: 무엇에 막혀도 2초 뒤 SIGALRM 으로 죽는다 */
}

/* ====================== Part A — 파일 매핑 ====================== */

/* TODO A1: fd 의 처음 len 바이트를 PROT_READ|PROT_WRITE 와 flags(MAP_SHARED 또는 MAP_PRIVATE)로 매핑.
 *          실패하면 NULL (errno 는 mmap 이 남긴 그대로). */
static char *map_file(int fd, size_t len, int flags) {
    (void)fd; (void)len; (void)flags;
    return NULL; /* TODO */
}

/* 표의 한 줄. 각 칸: 1 = 보인다, 0 = 안 보인다, -1 = 측정 못 함 */
typedef struct {
    int to_file;        /* (a) 이 매핑에 쓴 값이 pread() 로 보이나 */
    int to_other_map;   /* (b) 같은 파일의 다른 MAP_SHARED 매핑에 보이나 */
    int sees_write2;    /* (c) 매핑 뒤 pwrite() 로 파일을 바꾸면 이 매핑에 보이나 (이 매핑으로 안 쓴 페이지) */
    int after_munmap;   /* (d) munmap 뒤 파일에 (a) 의 값이 남았나 */
} file_row_t;

/* TODO A2: path 에 'A' 로 채운 2페이지 파일을 만들고(make_file), flags 로 매핑 m 과
 *          비교용 MAP_SHARED 매핑 o 를 만든 뒤:
 *            m[0] = 'W'                → (a) file_byte(fd,0)=='W'?  (b) o[0]=='W'?
 *            pwrite(fd,"F",1,PG)       → (c) m[PG]=='F'?
 *            munmap 둘 다              → (d) file_byte(fd,0)=='W'?
 *          끝나면 close + unlink. 성공 0, 실패 -1 (r 은 -1 로 남긴다). */
static int probe_file(const char *path, int flags, file_row_t *r) {
    r->to_file = r->to_other_map = r->sees_write2 = r->after_munmap = -1;
    (void)path; (void)flags;
    return -1; /* TODO */
}

/* ====================== Part B — 익명 매핑, fork, SIGBUS ====================== */

/* TODO B1: len 바이트 익명 매핑 (PROT_READ|PROT_WRITE, flags | MAP_ANON, fd = -1). 실패 NULL. */
static char *map_anon(size_t len, int flags) {
    (void)len; (void)flags;
    return NULL; /* TODO */
}

/* TODO B2: p[0] = 'A' 로 두고 fork. 자식은 p[0] = 'C' 쓰고 _exit(0).
 *          부모는 wait_child(pid, 2000) 후 p[0] 을 본다. 보이면 1, 아니면 0, 오류 -1. */
static int child_write_seen_by_parent(volatile char *p) {
    (void)p;
    return -1; /* TODO */
}

/* TODO B3: p[1] = 'A' 로 두고 pipe + fork. 자식은 child_catch_faults() 후 pipe 에서 1바이트를
 *          기다렸다가 p[1]=='P' 이면 _exit(1), 아니면 _exit(2). 부모는 fork 뒤에 p[1] = 'P' 를 쓰고
 *          pipe 에 1바이트 써서 자식을 깨운 다음 wait_child. 보이면 1, 아니면 0, 오류 -1. */
static int parent_write_seen_by_child(volatile char *p) {
    (void)p;
    return -1; /* TODO */
}

/* TODO B4: 자식 프로세스에서 p[0] 을 읽거나(write=false) 쓴다(write=true).
 *          자식은 child_catch_faults() 를 먼저 부른다. 부모는 wait_child(pid, 2000) 로 받아서
 *            정상 종료(코드 0)                 → 0
 *            코드 FAULT_EXIT_BASE + sig       → sig  (핸들러가 잡은 진짜 시그널 번호)
 *            시그널로 죽음                     → WTERMSIG
 *            그 밖(시간 초과 등)               → -1 */
static int touch_in_child(volatile char *p, bool write) {
    (void)p; (void)write; (void)wait_child; (void)child_catch_faults;
    return -1; /* TODO */
}

/* ====================== Part C — 파일 복사 두 가지 ====================== */

/* TODO C1: src → dst 를 bufsz 바이트 버퍼로 read/write 복사. dst 는 O_CREAT|O_TRUNC.
 *          짧은 write(부분 쓰기)와 EINTR 을 처리한다. 성공 0, 실패 -1. */
static int copy_rw(const char *src, const char *dst, size_t bufsz) {
    (void)src; (void)dst; (void)bufsz;
    return -1; /* TODO */
}

/* TODO C2: src → dst 를 mmap 으로 복사.
 *          src 는 O_RDONLY + PROT_READ 로 매핑, dst 는 O_RDWR|O_CREAT|O_TRUNC 로 열어 ftruncate 로
 *          크기를 먼저 만든 뒤 PROT_READ|PROT_WRITE + MAP_SHARED 로 매핑하고 memcpy.
 *          크기 0 파일은 mmap 하지 말 것(len 0 → EINVAL). 성공 0, 실패 -1. */
static int copy_mmap(const char *src, const char *dst) {
    (void)src; (void)dst;
    return -1; /* TODO */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static const char *yn(int v) { return v == 1 ? "yes" : v == 0 ? "no" : "?"; }

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static int write_pattern(const char *path, size_t n, uint32_t seed) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    unsigned char blk[8192];
    for (size_t off = 0; off < n; ) {
        size_t k = n - off < sizeof blk ? n - off : sizeof blk;
        for (size_t i = 0; i < k; i++) blk[i] = (unsigned char)xs32(&seed);
        if (write(fd, blk, k) != (ssize_t)k) { close(fd); return -1; }
        off += k;
    }
    return close(fd);
}

static bool files_equal(const char *a, const char *b) {
    int fa = open(a, O_RDONLY), fb = open(b, O_RDONLY);
    bool eq = fa >= 0 && fb >= 0 && file_size(fa) == file_size(fb);
    unsigned char x[8192], y[8192];
    while (eq) {
        ssize_t na = read(fa, x, sizeof x), nb = read(fb, y, sizeof y);
        if (na != nb || na < 0) { eq = false; break; }
        if (na == 0) break;
        if (memcmp(x, y, (size_t)na) != 0) eq = false;
    }
    if (fa >= 0) close(fa);
    if (fb >= 0) close(fb);
    return eq;
}

static void cleanup_dir(void) {
    DIR *d = opendir(g_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL)
            if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) unlink(tmp_path(e->d_name));
        closedir(d);
    }
    rmdir(g_dir);
}

static void test_part_a(void) {
    printf("-- Part A: file mapping (page size %ld)\n", PG);
    int fd = make_file(tmp_path("basic"), (size_t)PG, 'A');
    char *m = map_file(fd, (size_t)PG, MAP_SHARED);
    close(fd);                                              /* 매핑은 fd 와 별개로 살아 있다 */
    CHECK(m != NULL);
    CHECK(m && m[0] == 'A' && m[PG - 1] == 'A');
    if (m) munmap(m, (size_t)PG);
    fd = open(tmp_path("basic"), O_RDWR);
    errno = 0;
    CHECK(map_file(fd, 0, MAP_SHARED) == NULL && errno == EINVAL);          /* len 0 */
    close(fd);

    /* 읽기 전용 fd: SHARED+WRITE 는 거부, PRIVATE+WRITE 는 허용 (쓰기가 파일로 안 가니까) */
    fd = open(tmp_path("basic"), O_RDONLY);
    errno = 0;
    CHECK(map_file(fd, (size_t)PG, MAP_SHARED) == NULL && errno == EACCES);
    char *pv = map_file(fd, (size_t)PG, MAP_PRIVATE);
    if (pv) pv[0] = 'p';
    CHECK(pv && pv[0] == 'p' && file_byte(fd, 0) == 'A');
    if (pv) munmap(pv, (size_t)PG);
    close(fd);

    file_row_t sh, pr;
    CHECK(probe_file(tmp_path("probe_shared"), MAP_SHARED, &sh) == 0);
    CHECK(probe_file(tmp_path("probe_private"), MAP_PRIVATE, &pr) == 0);
    printf("  %-12s | (a) map write -> read() | (b) -> other SHARED map | (c) pwrite -> this map | (d) kept after munmap\n", "mapping");
    printf("  %-12s | %-23s | %-23s | %-22s | %s\n", "MAP_SHARED", yn(sh.to_file), yn(sh.to_other_map), yn(sh.sees_write2), yn(sh.after_munmap));
    printf("  %-12s | %-23s | %-23s | %-22s | %s\n", "MAP_PRIVATE", yn(pr.to_file), yn(pr.to_other_map), yn(pr.sees_write2), yn(pr.after_munmap));
    CHECK(sh.to_file == 1 && sh.to_other_map == 1 && sh.sees_write2 == 1 && sh.after_munmap == 1);
    CHECK(pr.to_file == 0 && pr.to_other_map == 0 && pr.after_munmap == 0);
#ifdef __APPLE__
    CHECK(pr.sees_write2 == 0);     /* macOS: PRIVATE 는 mmap 시점 스냅숏 (POSIX 는 미지정) */
#endif
}

static void test_part_b(void) {
    printf("-- Part B: anonymous mapping, fork, SIGBUS/SIGSEGV\n");
    char *a = map_anon((size_t)PG, MAP_SHARED);
    CHECK(a != NULL);
    CHECK(a && a[0] == 0 && a[PG - 1] == 0);                 /* 익명 매핑은 0 으로 채워져 나온다 */
    if (a) munmap(a, (size_t)PG);

    /* fork 표: 파일 SHARED / 파일 PRIVATE / 익명 SHARED / 익명 PRIVATE */
    static const char *names[4] = {"file SHARED", "file PRIVATE", "anon SHARED", "anon PRIVATE"};
    int c2p[4], p2c[4];
    for (int k = 0; k < 4; k++) {
        int flags = (k & 1) ? MAP_PRIVATE : MAP_SHARED, fd = -1;
        char *p;
        if (k < 2) { fd = make_file(tmp_path("fork"), (size_t)PG, 'A'); p = map_file(fd, (size_t)PG, flags); }
        else p = map_anon((size_t)PG, flags);
        c2p[k] = p ? child_write_seen_by_parent(p) : -1;
        p2c[k] = p ? parent_write_seen_by_child(p) : -1;
        if (p) munmap(p, (size_t)PG);
        if (fd >= 0) close(fd);
    }
    printf("  %-13s | child write -> parent | parent write (after fork) -> child\n", "mapping");
    for (int k = 0; k < 4; k++) printf("  %-13s | %-21s | %s\n", names[k], yn(c2p[k]), yn(p2c[k]));
    for (int k = 0; k < 4; k++) {
        int want = (k & 1) ? 0 : 1;                          /* SHARED 는 공유, PRIVATE 는 각자 사본 */
        CHECK(c2p[k] == want);
        CHECK(p2c[k] == want);
    }

    /* EOF 너머: 100 바이트 파일을 2 페이지로 매핑 (파일보다 길게 매핑하는 것 자체는 허용) */
    int fd = make_file(tmp_path("eof"), 100, 'B');
    char *m = map_file(fd, 2 * (size_t)PG, MAP_SHARED);
    bool tail0 = false;
    int s_in = -9, s_r = -9, s_w = -9, s_shr = -9, s_grow = -9, ft1 = -9, ft2 = -9, ft3 = -9, b200 = -9;
    long long sz = -9;
    if (m) {
        tail0 = m[50] == 'B' && m[100] == 0 && m[PG - 1] == 0;  /* 마지막 페이지의 꼬리는 0 */
        s_in = touch_in_child(m + 10, false);
        s_r = touch_in_child(m + PG + 10, false);
        s_w = touch_in_child(m + PG, true);
        m[200] = 'Z';                                         /* 꼬리에 쓰기: 파일 크기는 그대로 */
        sz = file_size(fd);
        ft1 = ftruncate(fd, 300);                             /* 늘리면 늘어난 부분은 0 (POSIX ftruncate) */
        b200 = file_byte(fd, 200);
        ft2 = ftruncate(fd, 0);                               /* 매핑 중에 파일을 줄이면 */
        s_shr = touch_in_child(m, false);
        ft3 = ftruncate(fd, PG + 1);                          /* 다시 늘리면 2번째 페이지도 생긴다 */
        s_grow = touch_in_child(m + PG + 5, false);
        munmap(m, 2 * (size_t)PG);
    }
    close(fd);
    printf("  EOF: inside=%d  next page read=%d write=%d  (SIGBUS=%d)  tail write -> size %lld, byte200 after grow=%d\n",
           s_in, s_r, s_w, SIGBUS, sz, b200);
    printf("  after ftruncate(0): read=%d   after ftruncate(PG+1): page 2 read=%d\n", s_shr, s_grow);
    CHECK(m != NULL && tail0);
    CHECK(s_in == 0);
    CHECK(s_r == SIGBUS && s_w == SIGBUS);
    CHECK(sz == 100 && ft1 == 0 && b200 == 0);
    CHECK(ft2 == 0 && s_shr == SIGBUS);
    CHECK(ft3 == 0 && s_grow == 0);

    /* munmap: 가운데 페이지만 떼어 내기 */
    char *t = map_anon(3 * (size_t)PG, MAP_PRIVATE);
    int un_bad = -9, un_bad_errno = 0, un_mid = -9, s0 = -9, s1 = -9, s2 = -9, ms = -9, ms_errno = 0, s_all = -9;
    if (t) {
        errno = 0;
        un_bad = munmap(t + 1, (size_t)PG);                   /* 주소는 페이지 정렬이어야 */
        un_bad_errno = errno;
        un_mid = munmap(t + PG, (size_t)PG);
        s0 = touch_in_child(t, true);
        s1 = touch_in_child(t + PG, false);
        s2 = touch_in_child(t + 2 * PG, true);
        errno = 0;
        ms = msync(t, 3 * (size_t)PG, MS_SYNC);               /* 구멍이 낀 범위 */
        ms_errno = errno;
        munmap(t, (size_t)PG);
        munmap(t + 2 * PG, (size_t)PG);
        s_all = touch_in_child(t, false);
    }
    printf("  munmap middle page: left=%d middle=%d right=%d (SIGSEGV=%d)  msync over hole=%d errno=%d  after full munmap=%d\n",
           s0, s1, s2, SIGSEGV, ms, ms_errno, s_all);
    CHECK(t != NULL && un_bad == -1 && un_bad_errno == EINVAL && un_mid == 0);
    CHECK(s0 == 0 && s1 == SIGSEGV && s2 == 0);
    CHECK(ms == -1 && ms_errno == ENOMEM);
    CHECK(s_all == SIGSEGV);

    /* msync 규칙 */
    fd = make_file(tmp_path("msync"), (size_t)PG, 'M');
    m = map_file(fd, (size_t)PG, MAP_SHARED);
    int ok_sync = 0, e_align = 0, e_flags = 0;
    if (m) {
        m[0] = 's';
        ok_sync = msync(m, (size_t)PG, MS_SYNC) == 0 && file_byte(fd, 0) == 's';
        errno = 0;
        e_align = msync(m + 1, 10, MS_SYNC) == -1 && errno == EINVAL;           /* 주소 정렬 */
        errno = 0;
        e_flags = msync(m, (size_t)PG, MS_SYNC | MS_ASYNC) == -1 && errno == EINVAL;
        munmap(m, (size_t)PG);
    }
    close(fd);
    CHECK(ok_sync);
    CHECK(e_align);
    CHECK(e_flags);
}

static void test_part_c(void) {
    printf("-- Part C: copy_rw vs copy_mmap (correctness only; speed -> demo_bench.c)\n");
    const size_t sizes[] = {0, 1, 100, (size_t)PG - 1, (size_t)PG, (size_t)PG + 1, 3 * (size_t)PG + 123, (1u << 20) + 7};
    const int ns = (int)(sizeof sizes / sizeof sizes[0]);
    int ok_rw = 0, ok_mm = 0, ok_small = 0;
    for (int i = 0; i < ns; i++) {
        const char *src = tmp_path("src"), *d1 = tmp_path("dst_rw"), *d2 = tmp_path("dst_mm"), *d3 = tmp_path("dst_small");
        if (write_pattern(src, sizes[i], 0x9E3779B9u + (uint32_t)i) != 0) continue;
        ok_rw += copy_rw(src, d1, 65536) == 0 && files_equal(src, d1);
        ok_mm += copy_mmap(src, d2) == 0 && files_equal(src, d2);
        size_t odd = sizes[i] < 65536 ? 7 : 4093;                         /* 페이지와 안 맞는 작은 버퍼 */
        ok_small += copy_rw(src, d3, odd) == 0 && files_equal(src, d3);
    }
    printf("  sizes tested: %d   copy_rw ok=%d  copy_rw(odd buf) ok=%d  copy_mmap ok=%d\n", ns, ok_rw, ok_small, ok_mm);
    CHECK(ok_rw == ns);
    CHECK(ok_small == ns);
    CHECK(ok_mm == ns);

    /* 이미 있던 더 긴 dst 는 잘려야 한다 */
    write_pattern(tmp_path("src"), 100, 7);
    int fd = make_file(tmp_path("dst_rw"), 3 * (size_t)PG, 'Z'); close(fd);
    fd = make_file(tmp_path("dst_mm"), 3 * (size_t)PG, 'Z'); close(fd);
    CHECK(copy_rw(tmp_path("src"), tmp_path("dst_rw"), 4096) == 0 && files_equal(tmp_path("src"), tmp_path("dst_rw")));
    CHECK(copy_mmap(tmp_path("src"), tmp_path("dst_mm")) == 0 && files_equal(tmp_path("src"), tmp_path("dst_mm")));

    /* 없는 src 는 -1 (죽지 않고) */
    CHECK(copy_rw(tmp_path("no_such_file"), tmp_path("dst_x"), 4096) == -1);
    CHECK(copy_mmap(tmp_path("no_such_file"), tmp_path("dst_y")) == -1);
}

int main(void) {
    alarm(60);                       /* 최후의 안전장치: 무엇이 잘못돼도 60초 뒤엔 끝난다 */
    PG = sysconf(_SC_PAGESIZE);
    snprintf(g_dir, sizeof g_dir, "/tmp/OS-B09_XXXXXX");
    if (!mkdtemp(g_dir)) { perror("mkdtemp"); return 1; }

    test_part_a();
    test_part_b();
    test_part_c();

    cleanup_dir();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
