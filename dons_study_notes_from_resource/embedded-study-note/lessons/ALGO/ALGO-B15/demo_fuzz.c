/* ALGO-B15 demo: 가난한 사람의 퍼저 (random-input loop) — solution.c 의 파서를 쓰레기 입력으로 두드린다.
 * Apple clang 에는 libFuzzer 런타임(libclang_rt.fuzzer_osx.a)이 없어 -fsanitize=fuzzer 가 링크 단계에서 실패한다.
 * 그래서 고정 시드 무작위 루프 + ASan/UBSan + 불변식 검사로 대신한다.
 * build: cc -std=c11 -g -O1 -fsanitize=address,undefined demo_fuzz.c -o demo_fuzz && ./demo_fuzz
 */
#define ALGO_B15_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 테스트 도우미는 안 쓴다 */
#include "solution.c"
#include <time.h>

#define ITERS 300000

static const char *const dict[] = {
    "SET", "set", "LED", "led", "ON", "off", "READ", "read", "WRITE", "HELP", "0x", "0X", "0x4000_0010",
    "4", "64", "65", "0", "7", "8", "-1", "+1", "_", "__", "0x_1", "1_", "99999999999", "4294967295",
    "4294967296", "0xFFFF_FFFC", "0xffffffff", "010", "abc", "\x7f", "\xff", "READX",
};
static const char seps[] = " \t\r\n";
static const char *const seeds[] = {
    "SET LED 3 ON", "READ 0x4000_0010 4", "WRITE 0x4000_0020 0xDEAD_BEEF", "HELP", "read 0xFFFF_FFC0 64",
};

static int gen(int mode, char *buf, size_t cap) {
    size_t n = 0;
    if (mode == 0) {                                   /* 완전 무작위 바이트 1..255 */
        size_t len = xorshift32() % 100;
        for (; n < len && n + 1 < cap; n++) buf[n] = (char)(1 + xorshift32() % 255);
    } else if (mode == 1) {                            /* 단어 수프: 사전 토큰 + 무작위 구분자 */
        int k = (int)(xorshift32() % 8);
        for (int i = 0; i < k; i++) {
            const char *w = dict[xorshift32() % (sizeof dict / sizeof dict[0])];
            size_t wl = strlen(w);
            if (n + wl + 2 >= cap) break;
            memcpy(buf + n, w, wl); n += wl;
            int ns = 1 + (int)(xorshift32() % 3);
            for (int j = 0; j < ns && n + 1 < cap; j++) buf[n++] = seps[xorshift32() % 4];
        }
    } else {                                           /* 정상 명령을 1~4번 변이 */
        const char *s = seeds[xorshift32() % (sizeof seeds / sizeof seeds[0])];
        n = strlen(s);
        memcpy(buf, s, n);
        int edits = 1 + (int)(xorshift32() % 4);
        for (int i = 0; i < edits; i++) {
            size_t pos = n ? xorshift32() % n : 0;
            char ch = (char)(0x20 + xorshift32() % 0x5F);
            switch (xorshift32() % 3) {
            case 0: if (n) buf[pos] = ch; break;                                   /* 바꾸기 */
            case 1: if (n + 2 < cap) { memmove(buf + pos + 1, buf + pos, n - pos); buf[pos] = ch; n++; } break;
            default: if (n) { memmove(buf + pos, buf + pos + 1, n - pos - 1); n--; } break; /* 지우기 */
            }
        }
    }
    buf[n] = '\0';
    return (int)n;
}

static bool invariants(cp_err_t e, const cmd_t *c) {
    if ((unsigned)e >= CP_ERR__COUNT) return false;
    if (e != CP_OK) return c->kind == CMD_NONE && c->err_tok >= -1 && c->err_tok < CMD_MAX_TOKENS;
    if (c->err_tok != -1) return false;
    switch (c->kind) {
    case CMD_HELP: return true;
    case CMD_SET_LED: return c->u.led.idx < LED_COUNT;
    case CMD_READ: return c->u.read.len >= 1 && c->u.read.len <= READ_MAX_LEN && (c->u.read.addr & 3u) == 0
                          && c->u.read.len - 1u <= UINT32_MAX - c->u.read.addr;
    case CMD_WRITE: return (c->u.write.addr & 3u) == 0;
    default: return false;
    }
}

/* 성공한 결과를 정규형 문자열로 다시 쓰고 다시 파싱 → 같은 구조체여야 한다 (왕복 성질) */
static bool roundtrip(const cmd_t *c) {
    char s[64];
    cmd_t d;
    switch (c->kind) {
    case CMD_HELP: snprintf(s, sizeof s, "HELP"); break;
    case CMD_SET_LED: snprintf(s, sizeof s, "SET LED %u %s", (unsigned)c->u.led.idx, c->u.led.on ? "ON" : "OFF"); break;
    case CMD_READ: snprintf(s, sizeof s, "READ 0x%lX %lu", (unsigned long)c->u.read.addr, (unsigned long)c->u.read.len); break;
    default: snprintf(s, sizeof s, "WRITE 0x%lX 0x%lX", (unsigned long)c->u.write.addr, (unsigned long)c->u.write.value); break;
    }
    return cmd_parse(s, &d) == CP_OK && memcmp(&d, c, sizeof d) == 0;
}

int main(void) {
    static const char *const mode_name[] = {"random bytes", "token soup", "mutated seeds"};
    long violations = 0;
    clock_t t0 = clock();
    for (int mode = 0; mode < 3; mode++) {
        long hist[CP_ERR__COUNT] = {0};
        for (int it = 0; it < ITERS; it++) {
            char in[128], a[128], b[128];
            gen(mode, in, sizeof in);
            memcpy(a, in, sizeof in);
            memcpy(b, in, sizeof in);
            cmd_t c1, c2;
            cp_err_t e1 = cmd_parse(a, &c1), e2 = cmd_parse(b, &c2);
            bool ok = invariants(e1, &c1) && e1 == e2 && memcmp(&c1, &c2, sizeof c1) == 0;
            if (ok && e1 == CP_OK) ok = roundtrip(&c1);
            if (!ok && violations++ < 5) printf("  VIOLATION mode %d: \"%s\"\n", mode, in);
            hist[e1]++;
        }
        printf("%-14s:", mode_name[mode]);
        for (int e = 0; e < CP_ERR__COUNT; e++)
            if (hist[e]) printf(" %s=%ld", cp_err_name[e], hist[e]);
        printf("\n");
    }

    /* ISR 줄 버퍼: 무작위 바이트 2,000,000개, 태스크는 무작위 시점에만 꺼낸다 */
    static linebuf_t lb;
    long lines = 0, toolong = 0, lbad = 0;
    for (long i = 0; i < 2000000; i++) {
        uint32_t r = xorshift32();
        uint8_t byte = (r % 16 == 0) ? (uint8_t)"\r\n\b\x7f"[(r >> 8) % 4] : (uint8_t)(r >> 16);
        lb_rx_byte(&lb, byte);
        if (lb.len > CMD_MAX_LINE) lbad++;
        if ((r >> 24) % 8 == 0) {
            char line[CMD_MAX_LINE + 1];
            lb_status_t st = lb_take(&lb, line, sizeof line);
            if (st == LB_LINE) {
                lines++;
                size_t n = strlen(line);
                if (n == 0 || n > CMD_MAX_LINE) lbad++;
                for (size_t j = 0; j < n; j++)
                    if (line[j] != '\t' && (line[j] < 0x20 || line[j] > 0x7E)) lbad++;
            } else if (st == LB_TOOLONG) {
                toolong++;
            }
        }
    }
    double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("linebuf       : lines=%ld toolong=%ld dropped_bytes=%lu junk_bytes=%lu bad=%ld\n", lines, toolong,
           (unsigned long)lb.dropped_bytes, (unsigned long)lb.junk_bytes, lbad);
    printf("parse invariant violations: %ld  (%d inputs x 3 modes, %.2f s CPU)\n", violations, ITERS, sec);
    return (violations || lbad) ? 1 : 0;
}
