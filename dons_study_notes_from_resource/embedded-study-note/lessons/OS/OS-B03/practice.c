/* OS-B03 추가 연습 — 한 번에 검증
 *
 *   연습 1  strace -c 표 파서: 시스템 콜 이름 → calls, 그리고 total 줄
 *   연습 2  fork 뒤 getpid 캐시: 자식의 첫 getpid 는 커널에 간다 (macOS libc)
 *   연습 3  선형 보간 백분위수 (R type 7 / numpy 기본) vs nearest-rank
 *   연습 4  시스템 콜 고정비 상각: 효율 X % 를 넘기는 최소 읽기 크기
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define _DARWIN_C_SOURCE
#include <ctype.h>
#include <errno.h>
#include <mach/mach.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 연습 1 — strace -c 표 파서 ----------------
 * 줄 형식: "% time  seconds  usecs/call  calls  [errors]  syscall"  (errors 칸은 비어 있을 수 있다)
 * total 줄은 마지막 칸이 "total". 헤더·구분선(-----)은 건너뛴다. 없으면 -1. */
static long strace_calls(const char *table, const char *name) {
    const char *line = table;
    while (line && *line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char buf[256];
        if (len < sizeof buf) {
            memcpy(buf, line, len);
            buf[len] = '\0';
            char *tok[8];
            int nt = 0;
            for (char *t = strtok(buf, " \t"); t && nt < 8; t = strtok(NULL, " \t")) tok[nt++] = t;
            /* 숫자로 시작하는 줄만: 5 칸(errors 없음) 또는 6 칸 */
            if ((nt == 5 || nt == 6) && isdigit((unsigned char)tok[0][0]) &&
                strcmp(tok[nt - 1], name) == 0)
                return strtol(tok[3], NULL, 10);
        }
        line = end ? end + 1 : NULL;
    }
    return -1;
}

/* Lima VM (aarch64 Ubuntu, glibc 2.43) 에서 `strace -c ./bench 100000 3` 의 실제 출력 일부 */
static const char k_strace[] =
    "% time     seconds  usecs/call     calls    errors syscall\n"
    "------ ----------- ----------- --------- --------- ----------------\n"
    " 37.29    6.719960           8    800000           clock_gettime\n"
    " 18.38    3.312401           8    400002           getppid\n"
    " 16.34    2.944799           7    400001           read\n"
    " 14.12    2.544649           6    400001           write\n"
    " 13.86    2.497742           6    400000           getpid\n"
    "  0.00    0.000016           5         3           close\n"
    "  0.00    0.000000           0         1         1 faccessat\n"
    "  0.00    0.000000           0         3           openat\n"
    "------ ----------- ----------- --------- --------- ----------------\n"
    "100.00   18.019567           7   2400038         1 total\n";

/* ---------------- 연습 2 — fork 뒤 getpid 캐시 ---------------- */
static int64_t unix_now(void) {
    task_events_info_data_t e;
    mach_msg_type_number_t c = TASK_EVENTS_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_EVENTS_INFO, (task_info_t)&e, &c) != KERN_SUCCESS) return -1;
    return e.syscalls_unix;
}

typedef struct { int64_t traps_first, traps_again; pid_t seen_pid; } child_report_t;

/* 자식에서 getpid 를 n 번씩 두 묶음 부르며 BSD 시스템 콜 수를 센다. 부모는 결과를 파이프로 받는다.
 * 자식은 alarm(5) 워치독, 부모는 반드시 waitpid. 성공 0. */
static int fork_getpid_probe(int n, child_report_t *out, pid_t *child_pid) {
    int p[2];
    if (pipe(p) != 0) return -1;
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return -1; }
    if (pid == 0) {
        alarm(5);
        close(p[0]);
        volatile pid_t sink = 0;
        child_report_t r;
        int64_t a = unix_now();
        for (int i = 0; i < n; i++) sink += getpid();
        int64_t b = unix_now();
        for (int i = 0; i < n; i++) sink += getpid();
        int64_t c = unix_now();
        r.traps_first = b - a;
        r.traps_again = c - b;
        r.seen_pid = getpid();
        (void)sink;
        ssize_t w = write(p[1], &r, sizeof r);
        _exit(w == (ssize_t)sizeof r ? 0 : 1);
    }
    close(p[1]);
    ssize_t got = 0;
    while (got < (ssize_t)sizeof *out) {
        ssize_t k = read(p[0], (char *)out + got, sizeof *out - (size_t)got);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        got += k;
    }
    close(p[0]);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    *child_pid = pid;
    return (got == (ssize_t)sizeof *out && WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

/* ---------------- 연습 3 — 선형 보간 백분위수 ----------------
 * h = (n − 1) × p / 100, lo = floor(h) → s[lo] + (h − lo)(s[lo+1] − s[lo]). n == 0 이면 NAN. */
static double percentile_lin(const double *s, size_t n, double p) {
    if (n == 0) return NAN;
    double h = (double)(n - 1) * p / 100.0;
    size_t lo = (size_t)floor(h);
    if (lo >= n - 1) return s[n - 1];
    return s[lo] + (h - (double)lo) * (s[lo + 1] - s[lo]);
}
static double percentile_nr(const double *s, size_t n, double p) {   /* 본문 Part A 와 같음 */
    if (n == 0) return NAN;
    double r = ceil(p * (double)n / 100.0);
    size_t rank = r < 1.0 ? 1 : (r > (double)n ? n : (size_t)r);
    return s[rank - 1];
}

/* ---------------- 연습 4 — 고정비 상각 ----------------
 * 읽기 1 회 = 고정비 syscall_ps + 바이트당 copy_ps_per_byte. 효율 = 복사 시간 / 전체 시간.
 * 효율 ≥ eff_pct % 가 되는 최소 바이트 수: bytes ≥ syscall_ps × eff / (copy_ps × (100 − eff)).
 * eff_pct ≥ 100 이나 copy_ps == 0 이면 0 (불가능/무의미). */
static uint64_t min_chunk_bytes(uint64_t syscall_ps, uint64_t copy_ps_per_byte, uint32_t eff_pct) {
    if (eff_pct >= 100 || copy_ps_per_byte == 0) return 0;
    uint64_t num = syscall_ps * eff_pct, den = copy_ps_per_byte * (100u - eff_pct);
    return (num + den - 1) / den;
}

int main(void) {
    /* 연습 1 */
    CHECK(strace_calls(k_strace, "getppid") == 400002);
    CHECK(strace_calls(k_strace, "clock_gettime") == 800000);   /* vDSO 3 종은 안 세졌다 */
    CHECK(strace_calls(k_strace, "faccessat") == 1);            /* errors 칸이 있는 줄 */
    CHECK(strace_calls(k_strace, "total") == 2400038);
    CHECK(strace_calls(k_strace, "nanosleep") == -1);
    CHECK(strace_calls(k_strace, "calls") == -1);               /* 헤더 줄은 숫자로 시작하지 않는다 */

    /* 연습 2 */
    volatile pid_t warm = getpid();                             /* 부모의 캐시는 이미 채워짐 */
    (void)warm;
    int64_t a = unix_now();
    for (int i = 0; i < 1000; i++) warm = getpid();
    int64_t b = unix_now();
    CHECK(b - a == 0);                                          /* 부모: 0 번 */
    child_report_t r = {-1, -1, 0};
    pid_t cp = 0;
    CHECK(fork_getpid_probe(1000, &r, &cp) == 0);
    CHECK(r.traps_first == 1 && r.traps_again == 0);            /* 자식: 첫 호출만 커널 */
    CHECK(r.seen_pid == cp && cp != getpid());                  /* 그리고 값은 자식 자신의 pid */

    /* 연습 3 */
    double s7[] = {100, 101, 101, 102, 103, 104, 229};          /* 본문 Part A 의 정렬된 7 run */
    CHECK(fabs(percentile_lin(s7, 7, 50) - 102.0) < 1e-9);
    CHECK(fabs(percentile_lin(s7, 7, 90) - 154.0) < 1e-9);      /* h = 5.4 → 104 + 0.4 × 125 */
    CHECK(fabs(percentile_nr(s7, 7, 90) - 229.0) < 1e-9);       /* nearest-rank 는 실제 표본 */
    double s4[] = {10, 20, 30, 40};
    CHECK(fabs(percentile_lin(s4, 4, 50) - 25.0) < 1e-9 && fabs(percentile_nr(s4, 4, 50) - 20.0) < 1e-9);
    CHECK(fabs(percentile_lin(s4, 4, 100) - 40.0) < 1e-9 && fabs(percentile_lin(s4, 4, 0) - 10.0) < 1e-9);

    /* 연습 4: 고정비 300 ns, 복사 50 ps/B (= 20 GB/s) */
    CHECK(min_chunk_bytes(300000, 50, 90) == 54000u);
    CHECK(min_chunk_bytes(300000, 50, 99) == 594000u);
    CHECK(min_chunk_bytes(300000, 50, 50) == 6000u);            /* 고정비 = 복사비 인 지점 */
    CHECK(min_chunk_bytes(300000, 50, 100) == 0u && min_chunk_bytes(300000, 0, 90) == 0u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
