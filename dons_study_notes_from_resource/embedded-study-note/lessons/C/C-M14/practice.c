/* C-M14 추가 연습 — 한 번에 검증
 * 연습 1: 인터럽트를 막은 채 부르는 now64() — 하드웨어 카운터 + 오버플로 펜딩 플래그
 * 연습 2: main → ISR 방향 공유: 더블 버퍼 + sig_atomic_t 인덱스
 * 연습 3: 중첩 카운터 방식 임계 구역 (FreeRTOS taskENTER_CRITICAL 스타일)
 * 연습 4: async-signal-safe 로깅 — snprintf 없이 16진수 변환 + write(2)
 */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void on_alrm(void (*h)(int)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = h;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
}

/* ---------- 연습 1: 마스크 중 now64 ---------- */
/* 가짜 32비트 하드웨어 카운터: 읽을 때마다 hw_step 만큼 시간이 흐르고, 넘치면 펜딩 플래그가 선다.
 * 오버플로 ISR(여기선 안 돈다 — 인터럽트가 막혀 있다고 가정)이 g_ovf_hi++ 하고 플래그를 지운다. */
static uint32_t hw_cnt, hw_step, g_ovf_hi;
static bool hw_ovf_pending;
static uint32_t CNT(void) {
    uint32_t v = hw_cnt, n = hw_cnt + hw_step;
    if (n < hw_cnt) hw_ovf_pending = true;          /* 자리넘침 → TIMx_SR.UIF 같은 플래그 */
    hw_cnt = n;
    return v;
}
static uint64_t now64_masked_naive(void) {          /* 펜딩만 보고 lo 를 다시 안 읽는 버전 */
    uint32_t hi = g_ovf_hi, lo = CNT();
    if (hw_ovf_pending) hi++;
    return ((uint64_t)hi << 32) | lo;
}
static uint64_t now64_masked(void) {                /* 정답: 펜딩이면 lo 를 다시 읽는다 */
    uint32_t hi = g_ovf_hi, lo = CNT();
    if (hw_ovf_pending) { lo = CNT(); hi++; }      /* 다시 읽은 lo 는 반드시 넘침 "이후" 값 */
    return ((uint64_t)hi << 32) | lo;
}
static void hw_set(uint32_t hi, uint32_t cnt, uint32_t step, bool pending) {
    g_ovf_hi = hi; hw_cnt = cnt; hw_step = step; hw_ovf_pending = pending;
}

/* ---------- 연습 2: 더블 버퍼 (main 이 쓰고 ISR 이 읽는다) ---------- */
static volatile uint32_t g_cfg[2][2];               /* [버퍼][0=hi,1=lo] — 알람 마감 시각 */
static volatile sig_atomic_t g_active;              /* ISR 이 읽을 버퍼 번호: 대입 한 번으로 전환 */
static volatile uint32_t g_single[2];               /* 비교용 단일 버퍼 */
static uint64_t g_isr_saw, g_isr_saw_single;
static int g_wr, g_fire_at = -1;
static void cfg_isr(int sig) {                      /* ISR: 현재 활성 버퍼를 읽는다 */
    (void)sig;
    int a = g_active;
    g_isr_saw = ((uint64_t)g_cfg[a][0] << 32) | g_cfg[a][1];
    g_isr_saw_single = ((uint64_t)g_single[0] << 32) | g_single[1];
}
static void wr32(volatile uint32_t *p, uint32_t v) {  /* 쓰기 직후 ISR 주입 지점 */
    *p = v;
    if (++g_wr == g_fire_at) raise(SIGALRM);
}
static void cfg_publish_single(uint64_t d) {
    wr32(&g_single[0], (uint32_t)(d >> 32));
    wr32(&g_single[1], (uint32_t)d);
}
static void cfg_publish(uint64_t d) {
    int next = !g_active;                           /* ISR 이 안 보는 쪽에 쓴다 */
    wr32(&g_cfg[next][0], (uint32_t)(d >> 32));
    wr32(&g_cfg[next][1], (uint32_t)d);
    g_active = next;                                /* 다 쓴 뒤 한 번에 전환 */
}

/* ---------- 연습 3: 중첩 카운터 임계 구역 ---------- */
static unsigned g_crit_depth;                       /* main 만 만진다 */
static volatile uint32_t g_runs;
static void count_isr(int sig) { (void)sig; g_runs = g_runs + 1u; }
static void crit_enter(void) {
    if (g_crit_depth++ == 0) {
        sigset_t b; sigemptyset(&b); sigaddset(&b, SIGALRM);
        sigprocmask(SIG_BLOCK, &b, NULL);
    }
}
static void crit_exit(void) {
    if (--g_crit_depth == 0) {
        sigset_t b; sigemptyset(&b); sigaddset(&b, SIGALRM);
        sigprocmask(SIG_UNBLOCK, &b, NULL);
    }
}

/* ---------- 연습 4: async-signal-safe 로깅 ---------- */
static int g_log_fd = -1;
static volatile uint32_t g_isr_val;
static size_t fmt_hex32(char *out, uint32_t v) {    /* "0x0000001F" — 라이브러리 함수 없음 */
    static const char hx[] = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 8; i++) out[2 + i] = hx[(v >> (28 - 4 * i)) & 0xFu];
    return 10;
}
static void log_isr(int sig) {
    (void)sig;
    char buf[16] = "T=";
    size_t n = 2 + fmt_hex32(buf + 2, g_isr_val);
    buf[n++] = '\n';
    (void)write(g_log_fd, buf, n);                  /* write 는 async-signal-safe, printf 는 아니다 */
}

int main(void) {
    /* 연습 1 */
    hw_set(5u, 1234u, 10u, false);                   /* 넘침 없음 */
    CHECK(now64_masked() == ((5ull << 32) | 1234u));
    hw_set(5u, 7u, 10u, true);                       /* 이미 넘쳤는데 ISR 은 아직 (막혀 있음) */
    CHECK(now64_masked() == ((6ull << 32) | 17u));
    hw_set(5u, 0xFFFFFFF0u, 0x20u, false);           /* lo 를 읽은 "직후" 넘침 */
    CHECK(now64_masked_naive() == ((6ull << 32) | 0xFFFFFFF0u));   /* 2^32 µs 앞으로 점프 (버그) */
    hw_set(5u, 0xFFFFFFF0u, 0x20u, false);
    CHECK(now64_masked() == ((6ull << 32) | 0x10u));               /* 다시 읽어 넘침 이후 값 */

    /* 연습 2 */
    on_alrm(cfg_isr);
    g_active = 0; g_cfg[0][0] = 0u; g_cfg[0][1] = 0xFFFFFFFFu;     /* 현재 마감 = 0x0_FFFFFFFF */
    g_single[0] = 0u; g_single[1] = 0xFFFFFFFFu;
    g_wr = 0; g_fire_at = 1;                          /* hi 만 쓴 직후 ISR */
    cfg_publish_single(0x100000000ull);
    CHECK(g_isr_saw_single == 0x1FFFFFFFFull);        /* 새 hi + 옛 lo = 찢어진 값 */
    g_wr = 0; g_fire_at = 1;
    cfg_publish(0x100000000ull);
    CHECK(g_isr_saw == 0xFFFFFFFFull);                /* ISR 은 옛 버퍼 → 일관된 옛 값 */
    raise(SIGALRM);
    CHECK(g_isr_saw == 0x100000000ull && g_active == 1);   /* 전환 후엔 새 값 */
    g_wr = 0; g_fire_at = 2;
    cfg_publish(0x2AAAAAAAAull);
    CHECK(g_isr_saw == 0x100000000ull && g_active == 0);   /* 쓰는 중엔 여전히 옛 값 */

    /* 연습 3 */
    on_alrm(count_isr);
    g_runs = 0u;
    crit_enter();
    crit_enter();                                     /* 중첩 */
    raise(SIGALRM);
    crit_exit();
    CHECK(g_runs == 0u && g_crit_depth == 1u);        /* 안쪽 exit 로는 안 풀린다 */
    crit_exit();
    CHECK(g_runs == 1u && g_crit_depth == 0u);        /* 바깥 exit 에서 펜딩 ISR 실행 */

    /* 연습 4 */
    char hex[11] = { 0 };
    fmt_hex32(hex, 0xDEADBEEFu);
    CHECK(strcmp(hex, "0xDEADBEEF") == 0);
    int fds[2];
    CHECK(pipe(fds) == 0);
    g_log_fd = fds[1];
    on_alrm(log_isr);
    g_isr_val = 0x1Fu; raise(SIGALRM);
    g_isr_val = 0xABCD1234u; raise(SIGALRM);
    char got[64] = { 0 };
    ssize_t n = read(fds[0], got, sizeof got - 1);
    CHECK(n == 26 && strcmp(got, "T=0x0000001F\nT=0xABCD1234\n") == 0);
    close(fds[0]); close(fds[1]);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
