/* TOOL-M09 demo_flag.c — 버그 2: 락 없이 읽는 플래그 (평범한 bool 로 busy-wait)
 *
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_flag.c -o df_t && ./df_t       # 보고 + 타임아웃 (루프가 안 끝남)
 *   cc -std=c11 -O2 -pthread demo_flag.c -o df && ./df                                # TSan 없이: 끝나지 않을 수 있다
 *   cc -std=c11 -O2 -S demo_flag.c -o - | grep -A12 '^_wait_ready:'                   # 왜 끝나지 않나 (asm)
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DVOL demo_flag.c -o df_vt && ./df_vt   # volatile: 끝나지만 보고 2개
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DVOL -DNEAR demo_flag.c -o df_nt && ./df_nt   # 같은 8 B 칸: 보고 1개뿐
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DFIX demo_flag.c -o df_ft && ./df_ft   # 고친 버전: 보고 없음
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

/* 두 변수를 서로 다른 캐시 라인에 둔다. -DNEAR 이면 같은 8바이트 안에 붙는다 →
 * TSan 은 첫 보고 뒤 그 8바이트 칸의 감시를 끄므로 두 번째 레이스(payload)를 놓친다. */
#ifdef NEAR
#define SEP                           /* 외부 링크 전역 → 최적화기가 쪼개거나 옮기지 않고 나란히 놓음 */
#else
#define SEP _Alignas(64) static
#endif
SEP int g_payload;             /* 일반 데이터: "센서 보정값" */
#ifdef FIX
SEP atomic_bool g_ready;       /* 고친 버전: release/acquire 로 게시 */
#elif defined(VOL)
SEP volatile bool g_ready;     /* "고친 척": volatile — 루프는 끝나지만 여전히 레이스 */
#else
SEP bool g_ready;                     /* 버그: 평범한 bool 플래그 */
#endif

__attribute__((noinline)) void wait_ready(void) {
#ifdef FIX
    while (!atomic_load_explicit(&g_ready, memory_order_acquire)) { }
#else
    while (!g_ready) { }              /* 락도 atomic 도 없는 busy-wait */
#endif
}

static void *calib_task(void *arg) {
    (void)arg;
    usleep(1000);                     /* 소비자가 먼저 기다리게 */
    g_payload = 42;                   /* 1) 데이터 */
#ifdef FIX
    atomic_store_explicit(&g_ready, true, memory_order_release);   /* 2) 게시 */
#else
    g_ready = true;                   /* 2) 플래그 — 순서·가시성 보장 없음 */
#endif
    return NULL;
}

static void on_alarm(int sig) {
    (void)sig;
    static const char msg[] = "TIMEOUT: wait_ready() never returned (flag hoisted out of the loop?)\n";
    ssize_t r = write(2, msg, sizeof msg - 1);
    (void)r;
    _exit(3);
}

int main(void) {
    signal(SIGALRM, on_alarm);
    alarm(2);                         /* 2초 안에 안 끝나면 타임아웃으로 종료 */
    pthread_t t;
    pthread_create(&t, NULL, calib_task, NULL);
    wait_ready();
    printf("payload=%d\n", g_payload);
    pthread_join(t, NULL);
    return 0;
}
