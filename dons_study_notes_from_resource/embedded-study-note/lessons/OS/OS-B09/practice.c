/* OS-B09 추가 연습 — 한 번에 검증
 *
 *   연습 1  펌웨어 이미지 CRC32 를 mmap 으로 (read 버전과 같은 값, 빈 파일 처리)
 *   연습 2  프로세스 간 공유 카운터: MAP_SHARED|MAP_ANON 위의 _Atomic (PRIVATE 이면 0)
 *   연습 3  읽기 전용 설정 테이블: mprotect 로 잠그고 쓰기 → 어떤 시그널? 다시 열기
 *   연습 4  커지는 매핑 로그: ftruncate + munmap/mmap 으로 키우기 (macOS 에는 mremap 이 없다)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
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

static long PG;
static char g_dir[64];
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static const char *tmp_path(const char *name) {
    static char buf[8][128];
    static int i;
    char *p = buf[i++ & 7];
    snprintf(p, sizeof buf[0], "%s/%s", g_dir, name);
    return p;
}
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
#define FAULT_EXIT_BASE 100
static void on_fault(int sig) { _exit(FAULT_EXIT_BASE + sig); }
static void child_catch_faults(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fault;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    alarm(2);
}
static int touch_in_child(volatile char *p, bool write) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        child_catch_faults();
        if (write) p[0] = 'x'; else { char c = p[0]; (void)c; }
        _exit(0);
    }
    int st = wait_child(pid, 2000);
    if (st < 0) return -1;
    if (WIFSIGNALED(st)) return WTERMSIG(st);
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return code == 0 ? 0 : code > FAULT_EXIT_BASE ? code - FAULT_EXIT_BASE : -1;
}

/* ====================== 연습 1 — CRC32 (IEEE 802.3, 반사형 0xEDB88320) ====================== */
static uint32_t crc32_update(uint32_t crc, const unsigned char *p, size_t n) {
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static int crc32_read(const char *path, uint32_t *out) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    unsigned char buf[65536];
    uint32_t crc = 0;
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) { if (errno == EINTR) continue; close(fd); return -1; }
        crc = crc32_update(crc, buf, (size_t)n);
    }
    close(fd);
    *out = crc;
    return 0;
}
static int crc32_mmap(const char *path, uint32_t *out) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return -1; }
    size_t n = (size_t)st.st_size;
    if (n == 0) { close(fd); *out = crc32_update(0, NULL, 0); return 0; }   /* len 0 mmap 은 EINVAL */
    void *p = mmap(NULL, n, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);                                                 /* 매핑은 fd 없이도 유효 */
    if (p == MAP_FAILED) return -1;
    *out = crc32_update(0, p, n);
    munmap(p, n);
    return 0;
}

/* ====================== 연습 2 — 프로세스 간 공유 카운터 ====================== */
/* flags = MAP_SHARED 또는 MAP_PRIVATE. nproc 개 자식이 각자 iters 번 +1. 부모가 본 최종값을 돌려준다. */
static long shared_counter(int flags, int nproc, int iters) {
    _Atomic unsigned *c = mmap(NULL, (size_t)PG, PROT_READ | PROT_WRITE, flags | MAP_ANON, -1, 0);
    if (c == MAP_FAILED) return -1;
    atomic_init(c, 0);
    pid_t pids[16];
    int started = 0;
    fflush(stdout);
    for (int i = 0; i < nproc && i < 16; i++) {
        pid_t pid = fork();
        if (pid < 0) break;
        if (pid == 0) {
            alarm(2);
            for (int k = 0; k < iters; k++) atomic_fetch_add_explicit(c, 1, memory_order_relaxed);
            _exit(0);
        }
        pids[started++] = pid;
    }
    bool ok = started == nproc;
    for (int i = 0; i < started; i++) {
        int st = wait_child(pids[i], 2000);
        ok = ok && st >= 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0;
    }
    long v = ok ? (long)atomic_load(c) : -1;
    munmap((void *)c, (size_t)PG);
    return v;
}

/* ====================== 연습 3 — 읽기 전용 설정 테이블 ====================== */
typedef struct { uint32_t magic, baud, gain[6]; } config_t;

static config_t *config_load_locked(void) {
    config_t *c = mmap(NULL, (size_t)PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (c == MAP_FAILED) return NULL;
    c->magic = 0xC0FFEE01u;
    c->baud = 115200;
    for (int i = 0; i < 6; i++) c->gain[i] = (uint32_t)(10 + i);
    if (mprotect(c, (size_t)PG, PROT_READ) != 0) { munmap(c, (size_t)PG); return NULL; }  /* 이후 쓰기 금지 */
    return c;
}

/* ====================== 연습 4 — 커지는 매핑 로그 ====================== */
typedef struct { int fd; char *map; size_t len, cap; } mlog_t;

static size_t round_up_pg(size_t n) { return (n + (size_t)PG - 1) / (size_t)PG * (size_t)PG; }

static int mlog_open(mlog_t *l, const char *path) {
    l->fd = open(path, O_RDWR | O_CREAT, 0644);
    if (l->fd < 0) return -1;
    struct stat st;
    if (fstat(l->fd, &st) < 0) { close(l->fd); return -1; }
    l->len = (size_t)st.st_size;                          /* 기존 내용 뒤에 이어 쓴다 */
    l->cap = round_up_pg(l->len > 0 ? l->len : 1);
    if (ftruncate(l->fd, (off_t)l->cap) < 0) { close(l->fd); return -1; }
    l->map = mmap(NULL, l->cap, PROT_READ | PROT_WRITE, MAP_SHARED, l->fd, 0);
    if (l->map == MAP_FAILED) { close(l->fd); return -1; }
    return 0;
}
static int mlog_append(mlog_t *l, const void *data, size_t n) {
    if (l->len + n > l->cap) {
        size_t ncap = l->cap;
        while (ncap < l->len + n) ncap *= 2;              /* 두 배씩: 다시 매핑하는 횟수 O(log n) */
        if (ftruncate(l->fd, (off_t)ncap) < 0) return -1; /* 파일을 먼저 키워야 새 페이지가 SIGBUS 아님 */
        munmap(l->map, l->cap);                           /* Linux 라면 mremap 으로 한 번에 */
        void *m = mmap(NULL, ncap, PROT_READ | PROT_WRITE, MAP_SHARED, l->fd, 0);
        if (m == MAP_FAILED) { l->map = NULL; return -1; }
        l->map = m;
        l->cap = ncap;
    }
    memcpy(l->map + l->len, data, n);
    l->len += n;
    return 0;
}
static int mlog_close(mlog_t *l) {
    int rc = 0;
    if (l->map) munmap(l->map, l->cap);
    if (ftruncate(l->fd, (off_t)l->len) < 0) rc = -1;     /* 꼬리의 0 을 잘라 정확한 크기로 */
    if (close(l->fd) < 0) rc = -1;
    return rc;
}

/* ---------------- 테스트 ---------------- */
static int write_bytes(const char *path, const void *p, size_t n) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    ssize_t w = n ? write(fd, p, n) : 0;
    close(fd);
    return w == (ssize_t)n ? 0 : -1;
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

int main(void) {
    alarm(60);
    PG = sysconf(_SC_PAGESIZE);
    snprintf(g_dir, sizeof g_dir, "/tmp/OS-B09_XXXXXX");
    if (!mkdtemp(g_dir)) { perror("mkdtemp"); return 1; }

    /* 연습 1 */
    uint32_t a = 1, b = 2;
    CHECK(write_bytes(tmp_path("img"), "123456789", 9) == 0);
    CHECK(crc32_mmap(tmp_path("img"), &a) == 0 && a == 0xCBF43926u);       /* CRC-32 표준 check 값 */
    CHECK(write_bytes(tmp_path("empty"), "", 0) == 0);
    CHECK(crc32_mmap(tmp_path("empty"), &a) == 0 && a == 0);
    {
        size_t n = 3 * (size_t)PG + 77;
        unsigned char *buf = malloc(n);
        uint32_t s = 12345;
        for (size_t i = 0; i < n; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; buf[i] = (unsigned char)s; }
        write_bytes(tmp_path("img2"), buf, n);
        free(buf);
        CHECK(crc32_mmap(tmp_path("img2"), &a) == 0 && crc32_read(tmp_path("img2"), &b) == 0 && a == b);
        printf("ex1: crc32(\"123456789\")=0xCBF43926 ok, %zu-byte image mmap=0x%08X read=0x%08X\n", n, a, b);
    }
    CHECK(crc32_mmap(tmp_path("nope"), &a) == -1);

    /* 연습 2 */
    long sh = shared_counter(MAP_SHARED, 4, 20000), pv = shared_counter(MAP_PRIVATE, 4, 20000);
    printf("ex2: 4 children x 20000 atomic increments: MAP_SHARED parent sees %ld, MAP_PRIVATE parent sees %ld\n", sh, pv);
    CHECK(sh == 80000);
    CHECK(pv == 0);

    /* 연습 3 */
    config_t *cfg = config_load_locked();
    CHECK(cfg != NULL);
    if (cfg) {
        int r = touch_in_child((volatile char *)&cfg->baud, false);
        int w = touch_in_child((volatile char *)&cfg->baud, true);
        printf("ex3: locked config read=%d write=%d (SIGBUS=%d SIGSEGV=%d)\n", r, w, SIGBUS, SIGSEGV);
        CHECK(r == 0 && cfg->baud == 115200 && cfg->magic == 0xC0FFEE01u);
#ifdef __APPLE__
        CHECK(w == SIGBUS);          /* macOS arm64 에서 관찰: 보호 위반 쓰기 = SIGBUS */
#else
        CHECK(w == SIGSEGV);         /* Linux mmap(2): read-only 영역 쓰기 = SIGSEGV */
#endif
        errno = 0;
        CHECK(mprotect((char *)cfg + 1, 16, PROT_READ | PROT_WRITE) == -1 && errno == EINVAL);  /* 주소 정렬 */
        CHECK(mprotect(cfg, (size_t)PG, PROT_READ | PROT_WRITE) == 0);
        cfg->baud = 921600;                                    /* 다시 열었으니 부모에서 직접 써도 된다 */
        CHECK(touch_in_child((volatile char *)&cfg->gain[0], true) == 0 && cfg->baud == 921600);
        munmap(cfg, (size_t)PG);
    }

    /* 연습 4 */
    {
        const char *path = tmp_path("log");
        size_t want_cap = 64 * 1024;
        char *want = malloc(want_cap);
        size_t wlen = 0;
        mlog_t l;
        int ok = mlog_open(&l, path) == 0;
        size_t remaps = 0, cap0 = ok ? l.cap : 0;
        for (int i = 0; ok && i < 3000; i++) {
            char rec[32];
            int n = snprintf(rec, sizeof rec, "rec %d\n", i);
            size_t before = l.cap;
            ok = mlog_append(&l, rec, (size_t)n) == 0;
            remaps += l.cap != before;
            memcpy(want + wlen, rec, (size_t)n); wlen += (size_t)n;
        }
        ok = ok && mlog_close(&l) == 0;
        ok = ok && mlog_open(&l, path) == 0;                   /* 다시 열어 이어 쓰기 */
        for (int i = 3000; ok && i < 5000; i++) {
            char rec[32];
            int n = snprintf(rec, sizeof rec, "rec %d\n", i);
            ok = mlog_append(&l, rec, (size_t)n) == 0;
            memcpy(want + wlen, rec, (size_t)n); wlen += (size_t)n;
        }
        ok = ok && mlog_close(&l) == 0;
        int fd = open(path, O_RDONLY);
        struct stat st;
        fstat(fd, &st);
        char *got = malloc(want_cap);
        ssize_t gn = read(fd, got, want_cap);
        close(fd);
        printf("ex4: 5000 records, %zu bytes, file size %lld, first cap %zu, remaps in first 3000 = %zu\n",
               wlen, (long long)st.st_size, cap0, remaps);
        CHECK(ok);
        CHECK((size_t)st.st_size == wlen && gn == (ssize_t)wlen && memcmp(got, want, wlen) == 0);
        CHECK(remaps >= 1 && remaps <= 4);
        free(got);
        free(want);
    }

    cleanup_dir();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
