/* OS-M02 컨텍스트 스위치 비용 측정: 파이프 핑퐁
 *
 *   Part A  1바이트 파이프 핑퐁: EINTR 재시도 read/write, 퐁(에코 +1), 핑(왕복 검증 + 시간 기록)
 *           → 두 스레드 / fork 한 두 프로세스에서 같은 함수로 돌린다
 *   Part B  측정 수학: tick → ns 변환(오버플로 없이), 워밍업 제외 min/median/p99/mean,
 *           시스템 콜 기준선 빼기로 스위치 비용 추정, 타이머 해상도에 맞춘 배치 크기
 *   Part C  센서 샘플 → 로거 스레드 파이프 전달: 배치 전송, 부분 읽기 재조립, 배치 크기 설계
 *
 * 체크는 "올바르면 매번 성립" 하는 것만 본다 (왕복 수, 에코 값, 통계 함수의 고정 입력 결과,
 * 모든 샘플이 순서대로 도착). 시간 값은 출력만 하고 CHECK 하지 않는다 — 실측은 demo_bench.c.
 * 하네스의 모든 대기는 EOF 로 끝나므로 TODO 가 비어 있어도 멈추지 않는다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _DARWIN_C_SOURCE           /* -std=c11 에서 fork/pipe + clock_gettime_nsec_np 선언 (include 전에) */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

/* 단조 시계 (ns). 맥의 CLOCK_MONOTONIC 은 1 µs 단위라 쓰지 않는다 (demo_timer.c). */
static uint64_t now_ns(void) {
#ifdef __APPLE__
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);      /* 24 MHz 카운터: 41.67 ns 단위 */
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}

/* ============================ Part A — 1바이트 핑퐁 ============================ */

/* 1바이트 쓰기/읽기. 시그널로 끊기면(EINTR) 다시 시도한다.
 * 반환: 1 = 성공, 0 = EOF(읽기만: 상대가 쓰기 끝을 모두 닫음), -1 = 오류 */
static int xwrite1(int fd, uint8_t b) {
    for (;;) {
        ssize_t n = write(fd, &b, 1);
        if (n == 1) return 1;
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
}
static int xread1(int fd, uint8_t *b) {
    for (;;) {
        ssize_t n = read(fd, b, 1);
        if (n == 1) return 1;
        if (n == 0) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
}

/* 퐁: 1바이트를 받아 +1 해서 돌려준다. rounds 번 하거나 EOF/오류면 멈춘다. 돌려준 횟수 반환. */
static unsigned pp_pong(int rfd, int wfd, unsigned rounds) {
    unsigned i = 0;
    uint8_t b;
    while (i < rounds && xread1(rfd, &b) == 1) {
        if (xwrite1(wfd, (uint8_t)(b + 1u)) != 1) break;
        i++;
    }
    return i;
}

/* 핑: i 번째 왕복에서 (uint8_t)i 를 보내고 (uint8_t)(i+1) 이 돌아와야 한다.
 * rt_ns != NULL 이면 왕복마다 걸린 시간(ns)을 rt_ns[i] 에 기록. 검증된 왕복 수 반환
 * (EOF, 오류, 틀린 값이면 그 자리에서 멈춘다). */
static unsigned pp_ping(int rfd, int wfd, unsigned rounds, uint64_t *rt_ns) {
    for (unsigned i = 0; i < rounds; i++) {
        uint8_t r;
        uint64_t t0 = now_ns();
        if (xwrite1(wfd, (uint8_t)i) != 1 || xread1(rfd, &r) != 1) return i;
        uint64_t t1 = now_ns();
        if (r != (uint8_t)(i + 1u)) return i;
        if (rt_ns) rt_ns[i] = t1 - t0;
    }
    return rounds;
}

/* ---- 하네스 (주어진 코드): 파이프 두 개 + 스레드 또는 fork ---- */
typedef struct { int rfd, wfd; unsigned rounds, served; } pong_arg_t;
static void *pong_main(void *arg) {
    pong_arg_t *a = arg;
    a->served = pp_pong(a->rfd, a->wfd, a->rounds);
    close(a->wfd);                         /* 내 쓰기 끝을 닫아야 핑이 (멈췄을 때) EOF 를 본다 */
    return NULL;
}
/* 두 스레드 핑퐁. pong_rounds > rounds 로 주면 "핑이 끝내고 닫으면 퐁이 EOF 로 끝나는지" 를 본다. */
static unsigned run_thread_pp(unsigned rounds, unsigned pong_rounds, uint64_t *rt, unsigned *served) {
    int a2b[2], b2a[2];
    if (pipe(a2b) || pipe(b2a)) return 0;
    pong_arg_t a = { a2b[0], b2a[1], pong_rounds, 0 };
    pthread_t t;
    pthread_create(&t, NULL, pong_main, &a);
    unsigned done = pp_ping(b2a[0], a2b[1], rounds, rt);
    close(a2b[1]);                         /* 핑의 쓰기 끝을 닫음 → 퐁의 read 가 0 (EOF) */
    pthread_join(t, NULL);
    close(a2b[0]); close(b2a[0]);
    if (served) *served = a.served;
    return done;
}
/* 두 프로세스 핑퐁. 자식은 퐁 횟수가 기대값과 같으면 exit(0). 쓰지 않는 끝은 반드시 닫는다. */
static unsigned run_proc_pp(unsigned rounds, uint64_t *rt, int *child_ok) {
    int a2b[2], b2a[2];
    if (pipe(a2b) || pipe(b2a)) return 0;
    fflush(stdout);                        /* 자식에 출력 버퍼가 복제되지 않게 */
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        close(a2b[1]); close(b2a[0]);      /* 자식: a2b 읽기, b2a 쓰기만 남긴다 */
        unsigned s = pp_pong(a2b[0], b2a[1], rounds + 5u);
        _exit(s == rounds ? 0 : 1);        /* 핑이 rounds 번 뒤 닫으면 EOF 로 끝나야 한다 */
    }
    close(a2b[0]); close(b2a[1]);          /* 부모: b2a 읽기, a2b 쓰기만 */
    unsigned done = pp_ping(b2a[0], a2b[1], rounds, rt);
    close(a2b[1]);
    int st = 0;
    waitpid(pid, &st, 0);
    close(b2a[0]);
    *child_ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    return done;
}

/* ============================ Part B — 측정 수학 ============================ */

/* mach_absolute_time tick → ns. ns = t * numer / denom (Apple Silicon: 125/3).
 * t * numer 는 64비트를 넘칠 수 있다 → 몫과 나머지로 나눠 계산 (t 가 어떤 값이어도 정확). */
static uint64_t ticks_to_ns(uint64_t t, uint32_t numer, uint32_t denom) {
    uint64_t q = t / denom, r = t % denom;
    return q * numer + r * numer / denom;
}

typedef struct { uint64_t min, med, p99, max, mean; size_t n; } lat_stats_t;

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
/* s[warm..n) 의 통계. 입력 배열은 바꾸지 않는다 (복사본을 정렬).
 * med = 정렬된 m 개 중 인덱스 (m-1)/2 (짝수면 낮은 쪽), p99 = nearest-rank: 인덱스 ceil(0.99*m)-1,
 * mean = 합 / m (내림). 남는 표본이 없으면 -1. */
static int summarize(const uint64_t *s, size_t n, size_t warm, lat_stats_t *out) {
    if (warm >= n) return -1;
    size_t m = n - warm;
    uint64_t *c = malloc(m * sizeof *c);
    if (!c) return -1;
    memcpy(c, s + warm, m * sizeof *c);
    qsort(c, m, sizeof *c, cmp_u64);
    uint64_t sum = 0;
    for (size_t i = 0; i < m; i++) sum += c[i];
    out->n = m;
    out->min = c[0];
    out->max = c[m - 1];
    out->med = c[(m - 1) / 2];
    out->p99 = c[(99u * m + 99u) / 100u - 1u];
    out->mean = sum / m;
    free(c);
    return 0;
}

/* 왕복 = 핑 write+read + 퐁 read+write = (write+read 쌍) 2개 + "블록 → 상대 깨움 → 스위치" 2번.
 * 블록하지 않는 write+read 쌍 비용(pair_ns, selfpipe 측정)을 빼고 2로 나눈 값 = 한 방향 깨움+스위치 추정.
 * 기준선이 더 크면 (측정이 잘못됐거나 둘이 다른 조건) 음수가 나온다 — 그대로 돌려준다. */
static int64_t switch_cost_ns(uint64_t rt_ns, uint64_t pair_ns) {
    return ((int64_t)rt_ns - 2 * (int64_t)pair_ns) / 2;
}

/* 타이머 한 칸(res_ns)이 측정값의 max_err_ppm 이하가 되도록, 한 표본에 묶을 연산 수(배치).
 * 조건: res_ns / (batch * op_ns) <= max_err_ppm / 1e6  →  batch >= res_ns * 1e6 / (op_ns * max_err_ppm).
 * 가장 작은 정수 batch (최소 1). op_ns 또는 max_err_ppm 이 0 이면 0 (정의 불가). */
static uint64_t batch_for_resolution(uint64_t res_ns, uint64_t op_ns, uint64_t max_err_ppm) {
    if (op_ns == 0 || max_err_ppm == 0) return 0;
    uint64_t num = res_ns * 1000000u, den = op_ns * max_err_ppm;
    uint64_t b = (num + den - 1u) / den;
    return b ? b : 1u;
}

/* ============================ Part C — 센서 → 로거 파이프 ============================ */

typedef struct { uint32_t seq; int16_t ax, ay, az; uint16_t crc; } sample_t;   /* 12 바이트 */
_Static_assert(sizeof(sample_t) == 12, "sample_t must be 12 bytes (no padding)");

static uint16_t sample_crc(const sample_t *s) {        /* 간단한 체크섬 (CRC 자리) */
    return (uint16_t)(s->seq * 31u + (uint16_t)s->ax * 7u + (uint16_t)s->ay * 5u + (uint16_t)s->az * 3u);
}
static sample_t make_sample(uint32_t seq) {
    sample_t s = { seq, (int16_t)(seq * 3u), (int16_t)-(int32_t)(seq % 1000u), (int16_t)(seq ^ 0x55u), 0 };
    s.crc = sample_crc(&s);
    return s;
}

/* 전부 쓰기: 짧은 쓰기(short write)와 EINTR 를 처리해 len 바이트를 다 쓴다. 성공 0, 실패 -1. */
static int write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/* n 개 샘플을 batch 개씩 묶어 한 번의 write_all 로 보낸다 (마지막 묶음은 짧을 수 있음).
 * 보낸 묶음 수 반환 (= 상대를 깨울 수 있는 기회 수). batch == 0 이면 0. 실패하면 그때까지의 수. */
static unsigned send_batched(int wfd, const sample_t *s, unsigned n, unsigned batch) {
    if (batch == 0) return 0;
    unsigned sent = 0;
    for (unsigned i = 0; i < n; i += batch) {
        unsigned k = n - i < batch ? n - i : batch;
        if (write_all(wfd, s + i, (size_t)k * sizeof *s) != 0) break;
        sent++;
    }
    return sent;
}

/* EOF 까지 읽어 out 에 최대 max 개 샘플로 재조립한다. read() 는 경계와 상관없이 아무 바이트 수나
 * 돌려줄 수 있다 (예: 12바이트 샘플의 5바이트만) → 남은 조각을 들고 있다가 이어 붙인다.
 * 한 번에 읽는 크기는 RD_CHUNK (일부러 12의 배수가 아님). 완성된 샘플 수 반환. */
#define RD_CHUNK 100
static unsigned recv_all(int rfd, sample_t *out, unsigned max) {
    uint8_t buf[RD_CHUNK + sizeof(sample_t)];
    size_t have = 0;                                  /* buf 에 남아 있는 조각 바이트 수 */
    unsigned cnt = 0;
    for (;;) {
        ssize_t n = read(rfd, buf + have, RD_CHUNK);
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;                            /* EOF (남은 조각은 버린다: 미완성 샘플) */
        have += (size_t)n;
        size_t off = 0;
        while (have - off >= sizeof(sample_t)) {
            if (cnt < max) memcpy(&out[cnt], buf + off, sizeof(sample_t));
            cnt += cnt < max;
            off += sizeof(sample_t);
        }
        memmove(buf, buf + off, have - off);
        have -= off;
    }
    return cnt;
}

/* 배치 크기 설계: 샘플이 rate_hz 로 생기고, 한 번 깨워 넘길 때 wake_ns 가 든다.
 *   CPU 오버헤드 (rate_hz / B) * wake_ns / 1e9  <=  cpu_budget_ppm / 1e6
 *   지연: 묶음의 첫 샘플은 (B-1)/rate_hz 초 기다린 뒤 wake_ns 후 도착 →  (B-1)*1e9/rate_hz + wake_ns <= deadline_ns
 * 둘 다 만족하는 가장 작은 B (>= 1) 를 반환, 없으면 0. 정수 연산으로 (부동소수 반올림 없이). */
static unsigned choose_batch(uint64_t rate_hz, uint64_t wake_ns, uint64_t cpu_budget_ppm, uint64_t deadline_ns) {
    if (rate_hz == 0 || cpu_budget_ppm == 0) return 0;
    /* B >= rate * wake / (budget_ppm * 1000)  (ns·Hz / 1e9 = 비율, 비율*1e6 = ppm) */
    uint64_t num = rate_hz * wake_ns, den = cpu_budget_ppm * 1000u;
    uint64_t b = (num + den - 1u) / den;
    if (b == 0) b = 1;
    if (wake_ns > deadline_ns) return 0;
    /* (b-1) * 1e9 <= (deadline - wake) * rate  인지 확인 (B 가 커질수록 지연만 늘어나므로 최소 b 만 보면 된다) */
    if ((b - 1u) * 1000000000u > (deadline_ns - wake_ns) * rate_hz) return 0;
    return (unsigned)b;
}

/* ---- Part C 하네스: 센서 스레드 → 파이프 → 로거(메인) ---- */
#define NSAMP 20000u
static sample_t g_tx[NSAMP], g_rx[NSAMP + 8];
typedef struct { int wfd; unsigned batch, sent; } tx_arg_t;
static void *sensor_thread(void *arg) {
    tx_arg_t *a = arg;
    a->sent = send_batched(a->wfd, g_tx, NSAMP, a->batch);
    close(a->wfd);                                   /* 다 보냈으면 닫는다 → 로거가 EOF 를 본다 */
    return NULL;
}
static void *dribble_thread(void *arg) {             /* 일부러 5바이트씩 쪼개 쓰는 "나쁜 UART 드라이버" */
    tx_arg_t *a = arg;
    const uint8_t *p = (const uint8_t *)g_tx;
    size_t len = 200u * sizeof(sample_t);
    for (size_t off = 0; off < len; off += 5u) write_all(a->wfd, p + off, len - off < 5u ? len - off : 5u);
    close(a->wfd);
    return NULL;
}
static unsigned rx_check(unsigned got, unsigned want) {     /* 순서·무결성 위반 수 */
    unsigned bad = got != want;
    for (unsigned i = 0; i < got && i < want; i++)
        bad += g_rx[i].seq != i || g_rx[i].crc != sample_crc(&g_rx[i]);
    return bad;
}
static unsigned run_logger(void *(*fn)(void *), unsigned batch, unsigned *sent) {
    int p[2];
    if (pipe(p)) return 0;
    memset(g_rx, 0, sizeof g_rx);
    tx_arg_t a = { p[1], batch, 0 };
    pthread_t t;
    pthread_create(&t, NULL, fn, &a);
    unsigned got = recv_all(p[0], g_rx, NSAMP + 8u);
    pthread_join(t, NULL);
    close(p[0]);
    if (sent) *sent = a.sent;
    return got;
}

/* ================================ 테스트 ================================ */
int main(void) {
    signal(SIGPIPE, SIG_IGN);   /* 읽는 쪽이 사라진 파이프에 쓰면 SIGPIPE 로 죽는 대신 EPIPE 를 받는다 */

    /* ---- Part A ---- */
    {
        int p[2];
        CHECK(pipe(p) == 0);
        uint8_t b = 0;
        CHECK(xwrite1(p[1], 0xA5) == 1 && xread1(p[0], &b) == 1 && b == 0xA5);
        close(p[1]);
        CHECK(xread1(p[0], &b) == 0);                         /* 쓰기 끝이 모두 닫힘 → EOF */
        close(p[0]);
    }
    {
        enum { R = 20000 };
        static uint64_t rt[R];
        unsigned served = 0;
        uint64_t w0 = now_ns();
        unsigned done = run_thread_pp(R, R + 5u, rt, &served);
        printf("thread ping-pong wall: %.2f ms for %u RT\n", (double)(now_ns() - w0) / 1e6, R);
        CHECK(done == R);                                     /* 모든 왕복이 검증됨 (byte 순환 포함) */
        CHECK(served == R);                                   /* 핑이 닫자 퐁은 EOF 로 끝남 */
        lat_stats_t st;
        if (done == R && summarize(rt, R, 1000, &st) == 0)
            printf("thread ping-pong: %u RT, min %llu  med %llu  p99 %llu ns (참고값, CHECK 안 함)\n", R,
                   (unsigned long long)st.min, (unsigned long long)st.med, (unsigned long long)st.p99);
        int child_ok = 0;
        done = run_proc_pp(R, rt, &child_ok);
        CHECK(done == R);
        CHECK(child_ok);                                      /* 자식도 정확히 R 번 돌려주고 EOF 로 끝남 */
        if (done == R && summarize(rt, R, 1000, &st) == 0)
            printf("proc   ping-pong: %u RT, min %llu  med %llu  p99 %llu ns (참고값, CHECK 안 함)\n", R,
                   (unsigned long long)st.min, (unsigned long long)st.med, (unsigned long long)st.p99);
        CHECK(run_thread_pp(300, 100, NULL, &served) == 100 && served == 100);   /* 퐁이 먼저 끝나면 핑은 EOF 로 멈춤 */
    }

    /* ---- Part B ---- */
    CHECK(ticks_to_ns(24000000u, 125, 3) == 1000000000u);    /* 24 MHz 카운터 1초 */
    CHECK(ticks_to_ns(1, 125, 3) == 41u && ticks_to_ns(3, 125, 3) == 125u);
    {
        uint64_t t = UINT64_MAX / 64u;                        /* t*125 는 64비트를 넘친다 */
        unsigned __int128 want = (unsigned __int128)t * 125u / 3u;
        CHECK(ticks_to_ns(t, 125, 3) == (uint64_t)want);
        CHECK(ticks_to_ns(123456789u, 1, 1) == 123456789u);  /* 인텔 맥 / 리눅스처럼 1:1 */
    }
    {
        uint64_t s[] = { 9000, 8000, 50, 40, 30, 20, 10, 60, 70, 80, 90, 100 };   /* 앞 2개 = 워밍업 */
        lat_stats_t st;
        CHECK(summarize(s, 12, 2, &st) == 0 && st.n == 10u);
        CHECK(st.min == 10u && st.max == 100u && st.med == 50u && st.mean == 55u);
        CHECK(st.p99 == 100u);                                /* m=10 → ceil(9.9)-1 = 9 */
        CHECK(s[0] == 9000u && s[2] == 50u);                  /* 입력은 그대로 */
        CHECK(summarize(s, 12, 12, &st) == -1);
        static uint64_t big[1000];
        for (unsigned i = 0; i < 1000; i++) big[i] = (i * 7919u) % 1000u + 1u;   /* 1..1000 순열 */
        CHECK(summarize(big, 1000, 0, &st) == 0 && st.med == 500u && st.p99 == 990u && st.max == 1000u);
        CHECK(st.mean == 500u);                               /* 500500/1000 = 500 (내림) */
        static uint64_t ref[997];                             /* 홀수 개, 워밍업 3: 하네스 정렬과 비교 */
        memcpy(ref, big + 3, sizeof ref);
        qsort(ref, 997, sizeof ref[0], cmp_u64);
        CHECK(summarize(big, 1000, 3, &st) == 0 && st.med == ref[498] && st.p99 == ref[987] && st.min == ref[0]);
    }
    CHECK(switch_cost_ns(2833, 432) == 984);                  /* (2833 - 864) / 2 */
    CHECK(switch_cost_ns(500, 432) < 0);                      /* 기준선 > 왕복: 측정 조건 불일치 */
    CHECK(batch_for_resolution(42, 100, 10000) == 42u);       /* 1% 오차: 42*1e6/(100*1e4) = 42 */
    CHECK(batch_for_resolution(1000, 3000, 1000) == 334u);    /* µs 시계로 3 µs 연산을 0.1%: 333.3 → 334 */
    CHECK(batch_for_resolution(42, 100000, 1000000) == 1u);   /* 연산이 해상도보다 훨씬 길면 1 */
    CHECK(batch_for_resolution(42, 0, 100) == 0u);

    /* ---- Part C ---- */
    for (unsigned i = 0; i < NSAMP; i++) g_tx[i] = make_sample(i);
    {
        unsigned sent = 0, got = run_logger(sensor_thread, 8, &sent);
        CHECK(got == NSAMP && rx_check(got, NSAMP) == 0);     /* 순서대로 전부, 깨짐 없음 */
        CHECK(sent == NSAMP / 8u);                            /* 8개씩 → 2500 묶음 */
        got = run_logger(sensor_thread, 7, &sent);
        CHECK(got == NSAMP && rx_check(got, NSAMP) == 0 && sent == (NSAMP + 6u) / 7u);   /* 마지막 묶음은 짧다 */
        got = run_logger(sensor_thread, 1000, &sent);         /* 12000 B > PIPE_BUF: 쓰기가 여러 조각 */
        CHECK(got == NSAMP && rx_check(got, NSAMP) == 0 && sent == 20u);
        got = run_logger(dribble_thread, 0, NULL);            /* 5바이트씩 들어와도 재조립 */
        CHECK(got == 200u && rx_check(got, 200u) == 0);
    }
    CHECK(choose_batch(4000, 1500, 1000, 2000000) == 6u);     /* 4 kHz, 깨움 1.5 µs, 0.1% CPU, 2 ms */
    CHECK(choose_batch(4000, 1500, 1000, 1000000) == 0u);     /* 1 ms 마감이면 B<=4 인데 CPU 는 B>=6 → 불가 */
    CHECK(choose_batch(1000, 1500, 10000, 5000000) == 1u);    /* 여유 있으면 1개씩 (지연 최소) */
    CHECK(choose_batch(32000, 3000, 1000, 10000000) == 96u);  /* 32 kHz IMU, E 코어 3 µs: 96 개 = 3 ms */
    CHECK(choose_batch(4000, 3000000, 1000000, 2000000) == 0u); /* 깨움 자체가 마감보다 길다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
