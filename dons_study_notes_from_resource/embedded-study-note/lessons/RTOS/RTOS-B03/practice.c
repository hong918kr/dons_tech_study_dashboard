/* RTOS-B03 추가 연습 — 한 번에 검증
 *
 *   연습 1  스케줄러 문맥 경유 양보 (task → sched → task) vs 직접 전환: swapcontext 횟수 비교
 *   연습 2  makecontext 인자로 64비트 포인터 넘기기: int 두 개(hi, lo)로 쪼개기
 *   연습 3  코루틴 UART 프레임 파서: 상태 기계 없이 "지역 변수 + 멈춘 자리" 가 상태가 된다
 *   연습 4  재귀 깊이별 스택 사용량 (칠하기 → high-water mark) → 프레임당 바이트
 *   연습 5  끝난 태스크 재시작: getcontext + makecontext 다시
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 *   (_XOPEN_SOURCE 600 은 파일 첫 줄에서 정의. ucontext deprecated 경고만 pragma 로 끈다)
 */
#define _XOPEN_SOURCE 600
#include <ucontext.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define STACK_SIZE (64 * 1024)
#define FILL_BYTE  0xA5u
static _Alignas(16) uint8_t g_stk[3][STACK_SIZE];

/* 공통: ctx 를 stack 위에서 fn() 으로 시작하게 만든다 (getcontext 먼저!) */
static int ctx_make(ucontext_t *uc, uint8_t *stack, size_t size, ucontext_t *link, void (*fn)(void)) {
    if (getcontext(uc) != 0) return -1;
    uc->uc_stack.ss_sp = stack; uc->uc_stack.ss_size = size; uc->uc_stack.ss_flags = 0;
    uc->uc_link = link;
    makecontext(uc, fn, 0);
    return 0;
}

/* ======================= 연습 1: 스케줄러 문맥 경유 ======================= */
static ucontext_t g_sched, g_main, g_tctx[2];
static int  g_cur1, g_done1[2];
static unsigned g_swaps;                       /* 모든 swapcontext 호출 수 */
static char g_ord1[64]; static size_t g_ol1;

static void swap_counted(ucontext_t *from, ucontext_t *to) { g_swaps++; swapcontext(from, to); }
static void yield_via_sched(void) { swap_counted(&g_tctx[g_cur1], &g_sched); }   /* 태스크 → 스케줄러 */
static void task1(void) {
    int me = g_cur1;
    for (int i = 0; i < 4; i++) {
        g_ord1[g_ol1++] = (char)('A' + me); g_ord1[g_ol1++] = (char)('0' + i); g_ord1[g_ol1] = 0;
        yield_via_sched();
    }
    g_done1[me] = 1;                           /* return → uc_link = &g_sched */
}
static void sched_loop(void) {                 /* 스케줄러도 자기 스택을 가진 문맥 */
    int alive = 2;
    g_cur1 = 0;
    while (alive > 0) {
        swap_counted(&g_sched, &g_tctx[g_cur1]);
        if (g_done1[g_cur1] == 1) { g_done1[g_cur1] = 2; alive--; }
        int nxt = 1 - g_cur1;
        if (g_done1[nxt] == 0) g_cur1 = nxt;   /* 상대가 살아 있으면 교대 */
    }
}                                              /* return → uc_link = &g_main */

static void ex1(void) {
    ctx_make(&g_sched, g_stk[2], STACK_SIZE, &g_main, sched_loop);
    ctx_make(&g_tctx[0], g_stk[0], STACK_SIZE, &g_sched, task1);
    ctx_make(&g_tctx[1], g_stk[1], STACK_SIZE, &g_sched, task1);
    swap_counted(&g_main, &g_sched);
    /* 직접 전환(solution.c 방식)이면 같은 8 step 에 swap 10번 (yield 8 + 커널 진입 2) */
    printf("ex1 via-scheduler: order=%s swaps=%u\n", g_ord1, g_swaps);
    CHECK(strcmp(g_ord1, "A0B0A1B1A2B2A3B3") == 0);
    CHECK(g_swaps == 1 + 2 * 8 + 2);           /* main→sched 1 + (sched→task, task→sched) × 8 step
                                                  + 마지막 진입 2 (그 뒤 return 은 uc_link 라 swap 아님) */
}

/* ======================= 연습 2: 64비트 포인터를 int 두 개로 ======================= */
static ucontext_t g_c2, g_m2;
static void *g_got2;
static void entry_hilo(int hi, int lo) {
    uintptr_t p = ((uintptr_t)(uint32_t)hi << 32) | (uintptr_t)(uint32_t)lo;
    g_got2 = (void *)p;
}
static void ex2(void) {
    static int target = 7;
    void *p = &target;
    uintptr_t u = (uintptr_t)p;
    getcontext(&g_c2);
    g_c2.uc_stack.ss_sp = g_stk[0]; g_c2.uc_stack.ss_size = STACK_SIZE; g_c2.uc_stack.ss_flags = 0;
    g_c2.uc_link = &g_m2;
    makecontext(&g_c2, entry_hilo, 2, (int)(uint32_t)(u >> 32), (int)(uint32_t)u);
    swapcontext(&g_m2, &g_c2);
    printf("ex2 pointer %p -> received %p\n", p, g_got2);
    CHECK(g_got2 == p && *(int *)g_got2 == 7);
}

/* ======================= 연습 3: 코루틴 UART 프레임 파서 ======================= */
static ucontext_t g_feeder, g_parser;
static int  g_in_byte;                         /* feeder → parser: 다음 바이트 (-1 = 스트림 끝) */
static char g_frames[4][16]; static int g_nframes, g_dropped;

static int next_byte(void) {                   /* 바이트가 필요하면 feeder 에게 양보하고 기다린다 */
    swapcontext(&g_parser, &g_feeder);
    return g_in_byte;
}
static void parser_task(void) {                /* '$' 로 시작, '\n' 로 끝, 최대 15 글자 */
    for (;;) {
        int c = next_byte();
        if (c < 0) return;
        if (c != '$') continue;                /* 프레임 시작까지 버린다 */
        char buf[16]; int n = 0; bool overflow = false;
        while ((c = next_byte()) >= 0 && c != '\n') {
            if (n < 15) buf[n++] = (char)c; else overflow = true;
        }
        if (c < 0) return;
        if (overflow) { g_dropped++; continue; }
        buf[n] = 0;
        if (g_nframes < 4) memcpy(g_frames[g_nframes++], buf, (size_t)n + 1);
    }
}
static void ex3(void) {
    const char *rx = "xx$IMU,12\n$BAT,87\n$THIS_LINE_IS_TOO_LONG\nnoise$GPS,OK\n";
    ctx_make(&g_parser, g_stk[1], STACK_SIZE, &g_feeder, parser_task);
    swapcontext(&g_feeder, &g_parser);         /* 파서를 첫 next_byte() 까지 돌린다 */
    for (size_t i = 0; rx[i]; i++) {          /* UART RX ISR 흉내: 바이트 하나씩 */
        g_in_byte = (unsigned char)rx[i];
        swapcontext(&g_feeder, &g_parser);
    }
    g_in_byte = -1;
    swapcontext(&g_feeder, &g_parser);         /* 파서가 return → uc_link = &g_feeder 로 돌아온다 */
    printf("ex3 frames=%d [%s] [%s] [%s], dropped=%d\n", g_nframes, g_frames[0], g_frames[1], g_frames[2], g_dropped);
    CHECK(g_nframes == 3 && g_dropped == 1);
    CHECK(strcmp(g_frames[0], "IMU,12") == 0 && strcmp(g_frames[1], "BAT,87") == 0 && strcmp(g_frames[2], "GPS,OK") == 0);
}

/* ======================= 연습 4: 재귀 깊이별 스택 사용량 ======================= */
static ucontext_t g_c4, g_m4;
static int g_depth4;
static volatile int g_sink4;
__attribute__((noinline)) static int dive(int d) {
    volatile char frame_buf[64];               /* 깊이마다 64 바이트를 실제로 쓴다 */
    for (int i = 0; i < 64; i++) frame_buf[i] = (char)(d + i);
    if (d == 0) return frame_buf[0];
    return dive(d - 1) + frame_buf[63];
}
static void dive_task(void) { g_sink4 = dive(g_depth4); }
static size_t used_for_depth(int d) {
    g_depth4 = d;
    ctx_make(&g_c4, g_stk[0], STACK_SIZE, &g_m4, dive_task);
    memset(g_stk[0], FILL_BYTE, STACK_SIZE);   /* makecontext 뒤에 칠한다 (macOS makecontext 는 0 으로 지운다) */
    swapcontext(&g_m4, &g_c4);
    size_t free_b = 0;
    while (free_b < STACK_SIZE && g_stk[0][free_b] == FILL_BYTE) free_b++;
    return STACK_SIZE - free_b;
}
static void ex4(void) {
    size_t u8 = used_for_depth(8), u16 = used_for_depth(16), u32 = used_for_depth(32);
    long per = (long)(u16 - u8) / 8;
    printf("ex4 used: depth8=%zu depth16=%zu depth32=%zu bytes -> %ld bytes/frame\n", u8, u16, u32, per);
    CHECK(u8 < u16 && u16 < u32);
    CHECK(per >= 64);                          /* 프레임 하나 ≥ 64 바이트 배열 */
    long d = (long)(u32 - u16) - 2 * (long)(u16 - u8);
    CHECK(d >= -16 && d <= 16);                /* 선형: 깊이 16 더 = 깊이 8 더의 두 배 */
}

/* ======================= 연습 5: 끝난 태스크 재시작 ======================= */
static ucontext_t g_c5, g_m5;
static int g_runs5;
static void once_task(void) { g_runs5++; }
static void ex5(void) {
    ctx_make(&g_c5, g_stk[0], STACK_SIZE, &g_m5, once_task);
    swapcontext(&g_m5, &g_c5);
    errno = 0;
    int r = swapcontext(&g_m5, &g_c5);         /* 끝난 문맥: macOS 는 -1/EINVAL (다른 libc 는 다를 수 있다) */
    int e = errno;
    ctx_make(&g_c5, g_stk[0], STACK_SIZE, &g_m5, once_task);   /* 재시작 = 문맥을 새로 만든다 */
    int r2 = swapcontext(&g_m5, &g_c5);
    printf("ex5 resume finished: ret=%d errno=%d; re-made: ret=%d runs=%d\n", r, e, r2, g_runs5);
#if defined(__APPLE__)
    CHECK(r == -1 && e == EINVAL);
#endif
    CHECK(r2 == 0 && g_runs5 == 2);
}

int main(void) {
    ex1(); ex2(); ex3(); ex4(); ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
