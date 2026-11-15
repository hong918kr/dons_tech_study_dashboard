/* TOOL-B11 추가 연습 — ASan 이 잡는 버그 3종 더 + 정적 풀(pool) 수동 poisoning
 *   ./p scope | ret | overlap | pool | pool_bad
 *   -DBUGGY 로 빌드하면 연습 1~3 의 "고치기 전" 코드가 들어간다 (check.sh 가 ASan 이
 *   정말 잡는지까지 확인). 기본 빌드 = 고친 코드.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HAVE_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)           /* GCC spelling */
#define HAVE_ASAN 1
#endif
#ifdef HAVE_ASAN
#include <sanitizer/asan_interface.h>
#define POISON(p, n)   ASAN_POISON_MEMORY_REGION((p), (n))
#define UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION((p), (n))
#else
#define POISON(p, n)   ((void)(p), (void)(n))     /* 펌웨어 빌드: 비용 0 */
#define UNPOISON(p, n) ((void)(p), (void)(n))
#endif

#define NOINLINE __attribute__((noinline))

/* ---- 연습 1: stack-use-after-scope — 블록 안 버퍼 주소가 블록 밖으로 ---- */
static NOINLINE size_t frame_tag(const uint8_t *f, size_t n, char *out, size_t outsz)
{
    const char *tag = "noise";
#ifndef BUGGY
    char tmp[12];                               /* 고침: 쓰는 곳과 같은 수명 */
#endif
    if (n >= 2 && f[0] == 0xA5u) {
#ifdef BUGGY
        char tmp[12];                           /* 버그: 이 블록이 끝나면 죽는다 */
#endif
        snprintf(tmp, sizeof tmp, "kind=%u", (unsigned)f[1]);
        tag = tmp;
    }
    size_t i = 0;
    for (; tag[i] != '\0' && i + 1 < outsz; i++)  /* 블록 밖에서 tag 를 읽는다 */
        out[i] = tag[i];
    out[i] = '\0';
    return i;
}

/* ---- 연습 2: stack-use-after-return — 지역 버퍼 주소를 전역에 저장 ---- */
static const char *g_last_err = "none";
#ifndef BUGGY
static char g_err_buf[16];                      /* 고침: 수명이 프로그램 전체 */
#endif

static NOINLINE void set_err(unsigned code)
{
#ifdef BUGGY
    char g_err_buf[16];                         /* 버그: return 하면 죽는 프레임 */
#endif
    snprintf(g_err_buf, sizeof g_err_buf, "E%03u", code);
    g_last_err = g_err_buf;
}

static NOINLINE size_t status_line(char *out, size_t outsz)
{
    volatile char scratch[64];                  /* 다른 함수가 같은 스택을 덮어쓴다 */
    for (size_t i = 0; i < sizeof scratch; i++)
        scratch[i] = (char)'#';
    return (size_t)snprintf(out, outsz, "err=%s", g_last_err);
}

/* ---- 연습 3: memcpy-param-overlap — 수신 줄 버퍼에서 앞 k 바이트 소비 ---- */
static NOINLINE size_t rx_consume(char *buf, size_t len, size_t k)
{
    if (k >= len)
        return 0;
#ifdef BUGGY
    memcpy(buf, buf + k, len - k);              /* 버그: 겹치는 구간에 memcpy */
#else
    memmove(buf, buf + k, len - k);             /* 고침: 겹쳐도 되는 memmove */
#endif
    return len - k;
}

/* ---- 연습 4: 정적 블록 풀 + 수동 poisoning ---- */
#define BLK  32u
#define NBLK 4u
static _Alignas(8) uint8_t g_pool[NBLK][BLK];
static uint8_t g_used[NBLK];

static void pool_init(void)
{
    memset(g_used, 0, sizeof g_used);
    POISON(g_pool, sizeof g_pool);              /* 처음엔 전부 "빌려주지 않음" */
}

static void *pool_alloc(size_t n)
{
    if (n == 0 || n > BLK)
        return NULL;
    for (unsigned i = 0; i < NBLK; i++)
        if (!g_used[i]) {
            g_used[i] = 1;
            UNPOISON(g_pool[i], n);             /* 요청한 n 바이트만 연다 */
            return g_pool[i];
        }
    return NULL;
}

static void pool_free(void *p)
{
    for (unsigned i = 0; i < NBLK; i++)
        if (p == g_pool[i]) {
            g_used[i] = 0;
            POISON(g_pool[i], BLK);             /* 반납 → 다시 접근 금지 */
        }
}

static NOINLINE int case_pool(int bad)
{
    pool_init();
    uint8_t *a = pool_alloc(20), *b = pool_alloc(8);
    memset(a, 0x11, 20);
    memset(b, 0x22, 8);
    if (bad)
        a[20] = 0x33;                           /* 의도적 오버플로 (같은 g_pool 안!) */
    unsigned sum = 0;
    for (unsigned i = 0; i < 20; i++)
        sum += a[i];
    pool_free(a);
    pool_free(b);
    uint8_t *c = pool_alloc(4);                 /* 반납된 첫 블록을 다시 빌린다 */
    printf("pool: sum=%u reuse=%s\n", sum, c == g_pool[0] ? "blk0" : "other");
    pool_free(c);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *c = argc > 1 ? argv[1] : "";
    char out[32];
    if (strcmp(c, "scope") == 0) {
        static const uint8_t f[] = { 0xA5, 2, 0, 0 };
        size_t n = frame_tag(f, sizeof f, out, sizeof out);
        printf("scope: tag=%s n=%zu\n", out, n);
        return 0;
    }
    if (strcmp(c, "ret") == 0) {
        set_err(42);
        status_line(out, sizeof out);
        printf("ret: %s\n", out);
        return 0;
    }
    if (strcmp(c, "overlap") == 0) {
        char line[24] = "AT+RST\r\nAT+GMR\r\n";
        size_t left = rx_consume(line, strlen(line) + 1, 8);
        printf("overlap: left=%zu next=%.6s\n", left, line);
        return 0;
    }
    if (strcmp(c, "pool") == 0)
        return case_pool(0);
    if (strcmp(c, "pool_bad") == 0)
        return case_pool(1);
    fprintf(stderr, "usage: %s scope|ret|overlap|pool|pool_bad\n", argv[0]);
    return 2;
}
