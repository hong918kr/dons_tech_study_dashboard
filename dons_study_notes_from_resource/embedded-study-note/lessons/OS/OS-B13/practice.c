/* OS-B13 추가 연습 — 한 번에 검증
 *
 *   연습 1  동시 저장 + 동시 읽기: 임시 이름을 프로세스별로(O_EXCL). 쓰는 프로세스 2개가 번갈아 저장하는 동안
 *           읽는 쪽은 항상 완전한 A 또는 B 만 본다 (rename 은 읽는 쪽에도 원자적). 제자리 덮어쓰기와 비교.
 *   연습 2  rename 없는 A/B 두 슬롯 (플래시식): 오래된 슬롯을 제자리에 덮어쓰고 fsync, 읽을 땐 CRC 가 맞는
 *           슬롯 중 seq 가 큰 것. 모든 연산 직후 SIGKILL 스윕 → 항상 옛 seq 또는 새 seq.
 *   연습 3  macOS renamex_np: RENAME_EXCL (있으면 EEXIST), RENAME_SWAP (새 것을 넣고 옛 것을 백업으로 받기).
 *   연습 4  원자적 저장의 부작용: 새 inode 라서 권한·하드 링크가 끊긴다 → fchmod 로 권한 유지, 하드 링크는 옛 내용.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define _DARWIN_C_SOURCE
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

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static char g_dir[64];
static int g_dirfd = -1;

/* ---------- 공통 유틸 ---------- */
static int write_all(int fd, const void *buf, size_t len) {
    const unsigned char *p = buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) { errno = EIO; return -1; }
        p += n; len -= (size_t)n;
    }
    return 0;
}
static long read_at(const char *name, unsigned char *buf, size_t cap) {    /* 길이, 없으면 -1 */
    int fd = openat(g_dirfd, name, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t n = 0;
    for (;;) {
        if (n == cap) break;
        ssize_t r = read(fd, buf + n, cap - n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        n += (size_t)r;
    }
    close(fd);
    return (long)n;
}
static bool wait_child(pid_t pid, int ms, int *st) {
    for (int t = 0; t < ms * 4; t++) {
        if (waitpid(pid, st, WNOHANG) == pid) return true;
        struct timespec ts = { 0, 250000 }; nanosleep(&ts, NULL);
    }
    kill(pid, SIGKILL);
    while (waitpid(pid, st, 0) < 0 && errno == EINTR) { }
    return false;
}
static void put_file(const char *name, const void *buf, size_t len, mode_t mode) {
    int fd = openat(g_dirfd, name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0 || write_all(fd, buf, len) < 0 || fsync(fd) < 0) perror("put_file");
    if (fd >= 0) close(fd);
}
static int count_with(const char *needle) {
    DIR *d = opendir(g_dir); int c = 0; struct dirent *e;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) if (strstr(e->d_name, needle)) c++;
    closedir(d);
    return c;
}

/* ======================= 연습 1 — 프로세스별 임시 이름 ======================= */
/* tmp 이름 = "<name>.<pid>.tmp", O_EXCL 로 남의 임시 파일을 절대 덮지 않는다. */
static int atomic_save_unique(const char *name, const void *buf, size_t len, mode_t mode) {
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s.%ld.tmp", name, (long)getpid());
    int fd = openat(g_dirfd, tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (fd < 0 && errno == EEXIST) {                /* 같은 pid 가 전에 죽으며 남긴 것 → 내 것이니 지우고 다시 */
        unlinkat(g_dirfd, tmp, 0);
        fd = openat(g_dirfd, tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    }
    if (fd < 0) return -1;
    if (write_all(fd, buf, len) < 0 || fsync(fd) < 0) {
        int e = errno; close(fd); unlinkat(g_dirfd, tmp, 0); errno = e; return -1;
    }
    if (close(fd) < 0 || renameat(g_dirfd, tmp, g_dirfd, name) < 0) {
        int e = errno; unlinkat(g_dirfd, tmp, 0); errno = e; return -1;
    }
    return fsync(g_dirfd);
}
static int inplace_save(const char *name, const void *buf, size_t len) {
    int fd = openat(g_dirfd, name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    int rc = 0;
    for (size_t off = 0; off < len && rc == 0; off += 512)         /* 512 B 씩: 중간 상태가 보이게 */
        rc = write_all(fd, (const unsigned char *)buf + off, len - off < 512 ? len - off : 512);
    close(fd);
    return rc;
}
static unsigned char VA[2000], VB[3000];
typedef struct { int ok, torn, missing, reads; } readstat_t;
/* 쓰는 자식 2 개 (A, B 를 rounds 번씩 저장) + 부모는 그동안 계속 읽어 분류한다 */
static readstat_t race(bool atomic, int rounds) {
    readstat_t rs = { 0, 0, 0, 0 };
    pid_t w[2];
    fflush(stdout);
    for (int i = 0; i < 2; i++) {
        w[i] = fork();
        if (w[i] == 0) {
            alarm(10);
            for (int k = 0; k < rounds; k++) {
                int rc = atomic ? atomic_save_unique("shared.cfg", i ? VB : VA, i ? sizeof VB : sizeof VA, 0644)
                                : inplace_save("shared.cfg", i ? VB : VA, i ? sizeof VB : sizeof VA);
                if (rc < 0) _exit(1);
            }
            _exit(0);
        }
    }
    static unsigned char buf[4096];
    int alive = 2, st, exits_ok = 0;
    bool reaped[2] = { false, false };
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (alive > 0) {
        long n = read_at("shared.cfg", buf, sizeof buf);
        rs.reads++;
        if (n < 0) rs.missing++;
        else if ((n == (long)sizeof VA && !memcmp(buf, VA, sizeof VA)) || (n == (long)sizeof VB && !memcmp(buf, VB, sizeof VB))) rs.ok++;
        else rs.torn++;
        for (int i = 0; i < 2; i++)
            if (!reaped[i] && waitpid(w[i], &st, WNOHANG) == w[i]) {
                reaped[i] = true; alive--;
                exits_ok += WIFEXITED(st) && WEXITSTATUS(st) == 0;
            }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (t1.tv_sec - t0.tv_sec > 8) break;       /* 워치독 */
    }
    for (int i = 0; i < 2; i++) if (!reaped[i]) { (void)wait_child(w[i], 0, &st); }
    if (exits_ok != 2) rs.missing += 1000000;       /* 쓰는 쪽이 실패했으면 확실히 FAIL 로 */
    return rs;
}

/* ======================= 연습 2 — A/B 두 슬롯 (rename 없음) ======================= */
/* 슬롯 레코드: [0] seq u32 | [4] value u32 | [8] crc32(앞 8 B) u32  — 리틀 엔디언 */
static uint32_t crc32_ieee(const void *data, size_t n) {
    const uint8_t *p = data; uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static const char *SLOT[2] = { "slot.a", "slot.b" };
static int g_kill_at, g_ops;
static void step(void) { if (++g_ops == g_kill_at) kill(getpid(), SIGKILL); }

static bool slot_read(int i, uint32_t *seq, uint32_t *val) {
    uint8_t r[16]; long n = read_at(SLOT[i], r, sizeof r);
    if (n != 12 || get32(r + 8) != crc32_ieee(r, 8)) return false;
    *seq = get32(r); *val = get32(r + 4);
    return true;
}
/* 유효한 슬롯 중 seq 가 큰 것. 없으면 false */
static bool ab_load(uint32_t *seq, uint32_t *val) {
    uint32_t s[2], v[2]; bool ok[2];
    for (int i = 0; i < 2; i++) ok[i] = slot_read(i, &s[i], &v[i]);
    if (!ok[0] && !ok[1]) return false;
    int pick = !ok[0] ? 1 : !ok[1] ? 0 : (s[1] > s[0] ? 1 : 0);
    *seq = s[pick]; *val = v[pick];
    return true;
}
/* 새 값을 "현재 아닌" 슬롯(무효거나 seq 가 작은 쪽)에 seq+1 로 제자리 쓰기 → fsync. 현재 슬롯은 손대지 않는다. */
static int ab_save(uint32_t val) {
    uint32_t s[2] = { 0, 0 }, v; bool ok[2];
    for (int i = 0; i < 2; i++) ok[i] = slot_read(i, &s[i], &v);
    int cur = !ok[0] ? (ok[1] ? 1 : -1) : !ok[1] ? 0 : (s[1] > s[0] ? 1 : 0);
    int dst = cur == 0 ? 1 : 0;
    uint32_t seq = cur < 0 ? 1 : s[cur] + 1;
    uint8_t r[12];
    put32(r, seq); put32(r + 4, val); put32(r + 8, crc32_ieee(r, 8));
    int fd = openat(g_dirfd, SLOT[dst], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    step();                                         /* open(O_TRUNC) 직후: 이 슬롯은 비었다 */
    for (int off = 0; off < 12; off += 4) {         /* 4 B 씩: 반쪽 레코드가 생길 수 있게 */
        if (write_all(fd, r + off, 4) < 0) { close(fd); return -1; }
        step();
    }
    if (fsync(fd) < 0) { close(fd); return -1; }
    step();
    close(fd);
    step();
    return fsync(g_dirfd);                          /* 처음 만든 슬롯이면 이름도 내려야 한다 */
}

/* ======================= 연습 4 — 권한 유지 ======================= */
/* 대상이 이미 있으면 그 권한 비트를 임시 파일에 fchmod 로 옮긴 뒤 rename */
static int atomic_save_keep_mode(const char *name, const void *buf, size_t len) {
    struct stat st;
    mode_t mode = 0644;
    if (fstatat(g_dirfd, name, &st, 0) == 0) mode = st.st_mode & 07777;
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s.tmp", name);
    int fd = openat(g_dirfd, tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    if (fchmod(fd, mode) < 0 || write_all(fd, buf, len) < 0 || fsync(fd) < 0) {   /* umask 와 무관하게 정확히 */
        int e = errno; close(fd); unlinkat(g_dirfd, tmp, 0); errno = e; return -1;
    }
    if (close(fd) < 0 || renameat(g_dirfd, tmp, g_dirfd, name) < 0) {
        int e = errno; unlinkat(g_dirfd, tmp, 0); errno = e; return -1;
    }
    return fsync(g_dirfd);
}

static void remove_all(void) {
    DIR *d = opendir(g_dir); struct dirent *e;
    if (d) {
        while ((e = readdir(d)) != NULL)
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) unlinkat(g_dirfd, e->d_name, 0);
        closedir(d);
    }
    close(g_dirfd);
    rmdir(g_dir);
}

int main(void) {
    snprintf(g_dir, sizeof g_dir, "/tmp/OS-B13_p.XXXXXX");
    if (!mkdtemp(g_dir) || (g_dirfd = open(g_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) < 0) { perror("setup"); return 1; }
    memset(VA, 'A', sizeof VA);
    memset(VB, 'B', sizeof VB);

    /* ---- 연습 1 ---- */
    put_file("shared.cfg", VA, sizeof VA, 0644);
    readstat_t ra = race(true, 150);
    printf("ex1 atomic : reads=%d ok=%d torn=%d missing=%d\n", ra.reads, ra.ok, ra.torn, ra.missing);
    CHECK(ra.reads > 0 && ra.torn == 0 && ra.missing == 0);           /* 읽는 쪽은 항상 완전한 A 또는 B */
    CHECK(count_with(".tmp") == 0);                                   /* 프로세스별 임시 파일도 전부 정리 */
    put_file("shared.cfg", VA, sizeof VA, 0644);
    readstat_t ri = race(false, 150);
    printf("ex1 inplace: reads=%d ok=%d torn=%d missing=%d   (not checked: timing-dependent)\n",
           ri.reads, ri.ok, ri.torn, ri.missing);
    CHECK(ri.missing < 1000000);                                      /* 쓰는 쪽 자체는 정상 종료 */

    /* ---- 연습 2 ---- */
    uint32_t seq = 0, val = 0;
    CHECK(!ab_load(&seq, &val));                                      /* 처음엔 아무것도 없음 */
    CHECK(ab_save(100) == 0 && ab_load(&seq, &val) && seq == 1 && val == 100);
    CHECK(ab_save(200) == 0 && ab_load(&seq, &val) && seq == 2 && val == 200);
    int ok = 0, bad = 0, runs = 0;
    printf("ex2 A/B sweep (seq2 -> seq3):");
    for (int k = 1; k <= 20; k++) {
        /* 매번 같은 출발점: 슬롯 a = seq1/100, 슬롯 b = seq2/200 → 다음 저장은 슬롯 a 를 덮는다 */
        uint8_t r[12];
        put32(r, 1); put32(r + 4, 100); put32(r + 8, crc32_ieee(r, 8)); put_file(SLOT[0], r, 12, 0644);
        put32(r, 2); put32(r + 4, 200); put32(r + 8, crc32_ieee(r, 8)); put_file(SLOT[1], r, 12, 0644);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) { alarm(5); g_kill_at = k; g_ops = 0; _exit(ab_save(300) == 0 ? 0 : 1); }
        int st = 0;
        bool fin = wait_child(pid, 2000, &st);
        bool done = fin && WIFEXITED(st) && WEXITSTATUS(st) == 0;
        runs++;
        if (ab_load(&seq, &val) && ((seq == 2 && val == 200) || (seq == 3 && val == 300))) ok++; else bad++;
        printf(" %s", ab_load(&seq, &val) ? (seq == 3 ? "seq3" : "seq2") : "NONE");
        if (done || !fin) break;
    }
    printf("\n");
    CHECK(runs == 7 && ok == runs && bad == 0);                       /* open, write×3, fsync, close + 끝 */
    CHECK(ab_load(&seq, &val) && seq == 3 && val == 300);

    /* ---- 연습 3 ---- */
    char p_new[128], p_cur[128];
    snprintf(p_new, sizeof p_new, "%s/x.new", g_dir);
    snprintf(p_cur, sizeof p_cur, "%s/x.cur", g_dir);
    put_file("x.new", "NEW", 3, 0644);
    CHECK(renamex_np(p_new, p_cur, RENAME_EXCL) == 0);               /* 없을 때만 → 성공 */
    put_file("x.new", "NEW2", 4, 0644);
    errno = 0;
    int rc = renamex_np(p_new, p_cur, RENAME_EXCL);
    CHECK(rc == -1 && errno == EEXIST);                               /* 이미 있으면 덮지 않는다 */
    unsigned char b[16]; long n;
    CHECK(renamex_np(p_new, p_cur, RENAME_SWAP) == 0);               /* 두 이름을 원자적으로 맞바꿈 */
    n = read_at("x.cur", b, sizeof b);
    CHECK(n == 4 && !memcmp(b, "NEW2", 4));
    n = read_at("x.new", b, sizeof b);
    CHECK(n == 3 && !memcmp(b, "NEW", 3));                            /* 옛 것이 백업으로 남는다 */
    printf("ex3 renamex_np: RENAME_EXCL -> EEXIST, RENAME_SWAP ok\n");

    /* ---- 연습 4 ---- */
    char p_cfg[128], p_alias[128];
    snprintf(p_cfg, sizeof p_cfg, "%s/perm.cfg", g_dir);
    snprintf(p_alias, sizeof p_alias, "%s/alias.cfg", g_dir);
    put_file("perm.cfg", "old", 3, 0644);
    chmod(p_cfg, 0600);
    CHECK(link(p_cfg, p_alias) == 0);                                 /* 하드 링크: 같은 inode 의 두 번째 이름 */
    struct stat s1, s2;
    stat(p_cfg, &s1);
    CHECK(atomic_save_keep_mode("perm.cfg", "new", 3) == 0);
    stat(p_cfg, &s2);
    CHECK((s2.st_mode & 07777) == 0600);                              /* 권한 유지 */
    CHECK(s2.st_ino != s1.st_ino);                                    /* 새 inode 다 */
    n = read_at("alias.cfg", b, sizeof b);
    CHECK(n == 3 && !memcmp(b, "old", 3));                            /* 하드 링크는 옛 내용을 계속 본다 */
    printf("ex4 inode %llu -> %llu, mode %o, alias still 'old'\n",
           (unsigned long long)s1.st_ino, (unsigned long long)s2.st_ino, (unsigned)(s2.st_mode & 07777));

    remove_all();
    errno = 0;
    CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
