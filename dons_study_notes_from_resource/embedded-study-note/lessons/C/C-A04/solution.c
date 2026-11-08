/* C-A04 재진입성과 스레드 안전: strtok vs strtok_r
 *
 * Part A: my_strtok_r (상태를 호출자가 들고 다님) / my_strtok (static 숨은 상태)
 *         → libc strtok_r 과 결정적 퍼즈(xorshift32, 고정 시드) 대조 + 중첩 토크나이징 테스트.
 * Part B: 세 가지 축 — 재진입(reentrant) / 스레드 안전(thread-safe) / ISR(async-signal) 안전.
 *         my_strtok_tls (_Thread_local 상태): 스레드끼리는 안전하지만 중첩·ISR 에는 여전히 깨진다.
 *         itoa_static (정적 버퍼, 주어짐) vs itoa_r (호출자 버퍼).
 * Part C: UART CLI 파서 cli_exec — ';' / 공백 / ',' / '=' 4단 중첩을 my_strtok_r 로.
 *         "ISR" 을 모든 토큰 경계에 하나씩 끼워 넣어도 결과가 같아야 한다.
 *
 * ISR 모델: 단일 코어에서 ISR 은 "main 의 두 명령 사이에 끼워 넣어진, 끝까지 실행되는 함수 호출" 이다.
 * 그래서 테스트는 시그널 대신 preempt_tick() 훅으로 n번째 토큰 호출 직후에 ISR 함수를 직접 부른다
 * → 결정적이고, 표준적으로도 깨끗하다 (진짜 비동기 시그널은 demo_*.c 에서).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
 */
#define _POSIX_C_SOURCE 200809L   /* -std=c11 에서 strtok_r 선언을 보이게 (모든 include 전에) */
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 주어진 도우미 ---------------- */
static int is_delim(char c, const char *delim) {     /* c 가 구분자 집합에 있나 (라이브러리 없이) */
    for (; *delim; delim++)
        if (*delim == c) return 1;
    return 0;
}

/* ---------------- Part A ---------------- */
/* strtok_r 과 같은 의미: s != NULL 이면 새 문자열 시작(*save 의 이전 값은 무시), s == NULL 이면 *save 부터.
 * 앞쪽 구분자를 건너뛰고, 토큰 끝의 구분자 1개를 '\0' 으로 바꾼 뒤 그 다음 위치를 *save 에 저장.
 * 토큰이 없으면 NULL (이후 같은 save 로 다시 불러도 NULL). 상태는 오직 *save 에만 둔다. */
static char *my_strtok_r(char *s, const char *delim, char **save) {
    if (s == NULL) s = *save;                       /* 이어서: 저장해 둔 위치부터 */
    if (s == NULL) return NULL;                     /* (방어) 시작 없이 NULL 로 불림 */
    while (*s && is_delim(*s, delim)) s++;          /* 앞쪽 구분자 건너뛰기 */
    if (*s == '\0') { *save = s; return NULL; }     /* 토큰 없음. 이후 호출도 NULL */
    char *tok = s;
    while (*s && !is_delim(*s, delim)) s++;         /* 토큰 끝 찾기 */
    if (*s) { *s = '\0'; *save = s + 1; }           /* 구분자 → NUL, 다음은 그 뒤부터 */
    else *save = s;                                 /* 문자열 끝: NUL 을 가리킨 채 (넘어가면 안 됨) */
    return tok;
}

/* strtok 과 같은 의미. 상태는 함수 안 static 변수 하나 — 모든 호출자(중첩, 스레드, ISR)가 공유한다. */
static char *my_strtok(char *s, const char *delim) {
    static char *last;
    return my_strtok_r(s, delim, &last);            /* 상태가 "모두의 것" 이라는 점만 다르다 */
}

/* ---------------- Part B ---------------- */
/* my_strtok 과 같지만 상태가 스레드마다 하나 (C11 _Thread_local, 블록 범위면 static 필수: 6.7.1p3). */
static char *my_strtok_tls(char *s, const char *delim) {
    static _Thread_local char *last;
    return my_strtok_r(s, delim, &last);            /* 스레드마다 다른 last, 같은 스레드 안에선 공유 */
}

/* 주어짐: 옛날식 itoa. 정적 버퍼를 돌려준다 → 다음 호출이 이전 결과를 덮어쓴다. */
static const char *itoa_static(int v) {
    static char buf[12];                           /* "-2147483648" + NUL */
    char tmp[12];
    int k = 0, i = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do { tmp[k++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (v < 0) buf[i++] = '-';
    while (k) buf[i++] = tmp[--k];
    buf[i] = '\0';
    return buf;
}

/* 재진입 판: 결과를 호출자 버퍼 buf[n] 에 쓰고 buf 반환. 자리가 모자라면 (n>0 이면 buf[0]='\0') NULL.
 * 가변 static 상태 금지. INT_MIN 포함 (부호 처리는 C-M10 참고). */
static char *itoa_r(int v, char *buf, size_t n) {
    char tmp[12];                                   /* 스택: 호출마다 자기 것 */
    size_t k = 0, i = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;   /* INT_MIN 도 안전 (C-M10) */
    do { tmp[k++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (k + (v < 0) + 1 > n) { if (n) buf[0] = '\0'; return NULL; }
    if (v < 0) buf[i++] = '-';
    while (k) buf[i++] = tmp[--k];
    buf[i] = '\0';
    return buf;
}

/* ---------------- ISR 주입 장치 (주어짐) ---------------- */
static void (*g_isr)(void);                        /* 끼워 넣을 "ISR"                    */
static int g_isr_at = -1;                          /* 몇 번째 토큰 호출 직후에 (-1 = 없음) */
static int g_ncall, g_in_isr, g_isr_runs;
static void inject(void (*isr)(void), int at) { g_isr = isr; g_isr_at = at; g_ncall = 0; g_isr_runs = 0; }
static void preempt_tick(void) {                   /* 실제 칩에서는: 아무 것도 없음 (IRQ 는 언제든 온다) */
    if (g_in_isr) return;                          /* ISR 안의 토큰 호출은 세지 않는다 */
    if (++g_ncall == g_isr_at && g_isr) { g_in_isr = 1; g_isr(); g_isr_runs++; g_in_isr = 0; }
}
typedef char *tok_fn(char *s, const char *delim);  /* my_strtok / my_strtok_tls / libc strtok */
static char *tok_s(tok_fn *f, char *s, const char *d) { char *t = f(s, d); preempt_tick(); return t; }
static char *tok_r(char *s, const char *d, char **save) { char *t = my_strtok_r(s, d, save); preempt_tick(); return t; }

static void cat(char *out, size_t n, const char *s) {   /* out 에 s 를 이어 붙임 (잘림 허용) */
    size_t k = 0;
    while (out[k]) k++;
    while (*s && k + 1 < n) out[k++] = *s++;
    out[k] = '\0';
}

/* ---------------- Part C ---------------- */
/* UART CLI 한 줄 실행. 형식:
 *   명령은 ';' 로 구분 (빈 명령 무시), 명령 안의 단어는 ' ' 또는 '\t' 로 구분, 첫 단어 = 이름.
 *   나머지 단어는 ',' 로 항목을 나누고, 각 항목은 '=' 로 나눈 조각을 ':' 로 이어 쓴다.
 *   출력: "이름(항목|항목|...)" 을 공백 하나로 이어서 out[n] 에. 반환: 명령 수.
 *   예) "led 1 on; pwm ch=2,duty=50 ;log" → "led(1|on) pwm(ch:2|duty:50) log()", 3
 * 모든 토큰 호출은 tok_r() 로 (ISR 주입 지점). line 은 파괴된다. */
static int cli_exec(char *line, char *out, size_t n) {
    char *s_cmd, *s_word, *s_item, *s_kv;           /* 중첩 단계마다 자기 save 포인터 */
    int ncmd = 0;
    if (n) out[0] = '\0';
    for (char *cmd = tok_r(line, ";", &s_cmd); cmd; cmd = tok_r(NULL, ";", &s_cmd)) {
        char *name = tok_r(cmd, " \t", &s_word);
        if (!name) continue;                        /* 공백뿐인 명령 */
        if (ncmd++) cat(out, n, " ");
        cat(out, n, name);
        cat(out, n, "(");
        int first = 1;
        for (char *word = tok_r(NULL, " \t", &s_word); word; word = tok_r(NULL, " \t", &s_word)) {
            for (char *item = tok_r(word, ",", &s_item); item; item = tok_r(NULL, ",", &s_item)) {
                if (!first) cat(out, n, "|");
                first = 0;
                int piece = 0;
                for (char *kv = tok_r(item, "=", &s_kv); kv; kv = tok_r(NULL, "=", &s_kv)) {
                    if (piece++) cat(out, n, ":");
                    cat(out, n, kv);
                }
            }
        }
        cat(out, n, ")");
    }
    return ncmd;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define STREQ(a, b) (strcmp((a), (b)) == 0)

static uint32_t xorshift32(uint32_t *s) {          /* 고정 시드 → 재현 가능한 퍼즈 */
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* 퍼즈: my_strtok_r 과 libc strtok_r 을 같은 입력 사본에 돌려 반환 오프셋·버퍼 내용을 비교 */
static int fuzz_vs_libc(int cases) {
    static const char alpha[] = "ab,; \t";
    static const char *const delims[] = { ",", ", ", ";, \t", " ", "", "x", "ab" };
    uint32_t seed = 0xC0A04u;
    int bad = 0;
    for (int c = 0; c < cases; c++) {
        char a[24], b[24];
        int len = (int)(xorshift32(&seed) % 21u);
        for (int i = 0; i < len; i++) a[i] = alpha[xorshift32(&seed) % (sizeof alpha - 1)];
        a[len] = '\0';
        memcpy(b, a, sizeof a);
        char *sa = NULL, *sb = NULL;
        for (int k = 0; k < 12; k++) {             /* 매 호출 구분자 집합을 바꿀 수도 있다 (7.24.5.8p2) */
            const char *d = delims[xorshift32(&seed) % (sizeof delims / sizeof delims[0])];
            char *ta = my_strtok_r(k ? NULL : a, d, &sa);
            char *tb = strtok_r(k ? NULL : b, d, &sb);
            if ((ta == NULL) != (tb == NULL) || (ta && ta - a != tb - b) || memcmp(a, b, sizeof a) != 0) {
                bad++;
                break;
            }
            if (!ta && k > 0) break;
        }
    }
    return bad;
}

/* 2단 중첩: ';' 로 그룹, ',' 로 항목. 결과 "[a=1|b=2][c=3|d=4]" */
static void nested_static(tok_fn *f, char *s, char *out, size_t n) {
    out[0] = '\0';
    for (char *g = tok_s(f, s, ";"); g; g = tok_s(f, NULL, ";")) {
        cat(out, n, "[");
        int first = 1;
        for (char *kv = tok_s(f, g, ","); kv; kv = tok_s(f, NULL, ",")) {
            if (!first) cat(out, n, "|");
            cat(out, n, kv);
            first = 0;
        }
        cat(out, n, "]");
    }
}
static void nested_r(char *s, char *out, size_t n) {
    char *sg, *si;                                 /* 단계마다 자기 save 포인터 */
    out[0] = '\0';
    for (char *g = tok_r(s, ";", &sg); g; g = tok_r(NULL, ";", &sg)) {
        cat(out, n, "[");
        int first = 1;
        for (char *kv = tok_r(g, ",", &si); kv; kv = tok_r(NULL, ",", &si)) {
            if (!first) cat(out, n, "|");
            cat(out, n, kv);
            first = 0;
        }
        cat(out, n, "]");
    }
}

/* Part B: main 이 "m0 m1 m2 m3" 을 자르는 도중 ISR 이 자기 버퍼 "x y z" 를 자른다 */
static tok_fn *g_cur;                              /* static 계열 테스트에서 main·ISR 이 쓰는 토크나이저 */
static char g_isr_buf[16], g_isr_out[32];
static void isr_tok_static(void) {
    memcpy(g_isr_buf, "x y z", 6);
    g_isr_out[0] = '\0';
    for (char *t = g_cur(g_isr_buf, " "); t; t = g_cur(NULL, " ")) { cat(g_isr_out, sizeof g_isr_out, t); cat(g_isr_out, sizeof g_isr_out, "|"); }
}
static void isr_tok_r(void) {
    char *sv;                                      /* ISR 자기 스택의 상태 */
    memcpy(g_isr_buf, "x y z", 6);
    g_isr_out[0] = '\0';
    for (char *t = my_strtok_r(g_isr_buf, " ", &sv); t; t = my_strtok_r(NULL, " ", &sv)) { cat(g_isr_out, sizeof g_isr_out, t); cat(g_isr_out, sizeof g_isr_out, "|"); }
}
static void main_flat_static(tok_fn *f, char *out, size_t n) {
    char line[] = "m0 m1 m2 m3";
    out[0] = '\0';
    for (char *t = tok_s(f, line, " "); t; t = tok_s(f, NULL, " ")) { cat(out, n, t); cat(out, n, "|"); }
}
static void main_flat_r(char *out, size_t n) {
    char line[] = "m0 m1 m2 m3", *sv;
    out[0] = '\0';
    for (char *t = tok_r(line, " ", &sv); t; t = tok_r(NULL, " ", &sv)) { cat(out, n, t); cat(out, n, "|"); }
}

/* Part B: itoa 를 쓰는 ISR */
static char g_isr_num[16];
static void isr_itoa_static(void) { const char *p = itoa_static(-45); memcpy(g_isr_num, p, strlen(p) + 1); }
static void isr_itoa_r(void) { char b[12]; if (itoa_r(-45, b, sizeof b)) memcpy(g_isr_num, b, strlen(b) + 1); }

/* Part B: 스레드 테스트 — 스레드마다 "T<id>a T<id>b ..." 를 ITER 번 자르고 틀린 토큰 수를 센다 */
#define NTHR 4
#define ITER 3000
typedef struct { int id; int use_tls; int errors; } thr_arg_t;
static void *thr_main(void *p) {
    thr_arg_t *a = p;
    for (int it = 0; it < ITER; it++) {
        char line[64], *sv = NULL;
        snprintf(line, sizeof line, "T%da,T%db;;T%dc T%dd", a->id, a->id, a->id, a->id);
        int k = 0;
        for (char *t = a->use_tls ? my_strtok_tls(line, ",; ") : my_strtok_r(line, ",; ", &sv); t;
             t = a->use_tls ? my_strtok_tls(NULL, ",; ") : my_strtok_r(NULL, ",; ", &sv), k++) {
            char want[8];
            snprintf(want, sizeof want, "T%d%c", a->id, 'a' + k);
            if (k >= 4 || !STREQ(t, want)) a->errors++;
        }
        if (k != 4) a->errors++;
    }
    return NULL;
}
static int run_threads(int use_tls) {
    pthread_t th[NTHR];
    thr_arg_t args[NTHR];
    int errors = 0;
    for (int i = 0; i < NTHR; i++) {
        args[i] = (thr_arg_t){ i, use_tls, 0 };
        pthread_create(&th[i], NULL, thr_main, &args[i]);
    }
    for (int i = 0; i < NTHR; i++) { pthread_join(th[i], NULL); errors += args[i].errors; }
    return errors;
}

/* Part C: CLI 파싱 도중 끼어드는 텔레메트리 ISR — 같은 도우미(my_strtok_r, itoa_r)를 자기 상태로 쓴다 */
static char g_tel_cfg[24], g_tel_out[32];
static void telemetry_isr(void) {
    char *sv, num[12];
    memcpy(g_tel_cfg, "id=7,rate=10", 13);
    g_tel_out[0] = '\0';
    for (char *t = my_strtok_r(g_tel_cfg, ",=", &sv); t; t = my_strtok_r(NULL, ",=", &sv)) {
        cat(g_tel_out, sizeof g_tel_out, t);
        cat(g_tel_out, sizeof g_tel_out, "/");
    }
    if (itoa_r(-273, num, sizeof num)) cat(g_tel_out, sizeof g_tel_out, num);
}

int main(void) {
    char buf[96], out[128];

    /* Part A — 기본 의미 (C11 7.24.5.8 의 EXAMPLE 포함) */
    char *sv = NULL;
    char ex[] = "?a???b,,,#c";
    char *t1 = my_strtok_r(ex, "?", &sv);
    char *t2 = my_strtok_r(NULL, ",", &sv);
    char *t3 = my_strtok_r(NULL, "#,", &sv);
    char *t4 = my_strtok_r(NULL, "?", &sv);
    CHECK(t1 && STREQ(t1, "a") && t2 && STREQ(t2, "??b") && t3 && STREQ(t3, "c") && t4 == NULL);
    CHECK(my_strtok_r(NULL, "?", &sv) == NULL);              /* 끝난 뒤 다시 불러도 NULL */
    char e1[] = "", e2[] = ",,,", e3[] = "abc";
    CHECK(my_strtok_r(e1, ",", &sv) == NULL && my_strtok_r(NULL, ",", &sv) == NULL);
    CHECK(my_strtok_r(e2, ",", &sv) == NULL && my_strtok_r(NULL, ",", &sv) == NULL);
    char *w = my_strtok_r(e3, "", &sv);                       /* 빈 구분자 집합 → 전체가 토큰 하나 */
    CHECK(w == e3 && STREQ(w, "abc") && my_strtok_r(NULL, "", &sv) == NULL);
    char e4[] = "a,b,", *x0 = NULL;
    char *p0 = my_strtok_r(e4, ",", &x0);
    char *p1 = my_strtok_r(NULL, ",", &x0);
    CHECK(p0 == e4 && p1 == e4 + 2 && my_strtok_r(NULL, ",", &x0) == NULL && memcmp(e4, "a\0b\0", 5) == 0);
    char e5[] = "ab", *junk = e4;                             /* s != NULL 이면 *save 의 이전 값은 무시 */
    CHECK(my_strtok_r(e5, ",", &junk) == e5 && my_strtok_r(NULL, ",", &junk) == NULL);
    CHECK(fuzz_vs_libc(5000) == 0);                           /* libc strtok_r 과 5000 케이스 동일 */
    char s1[] = " lo  hi ";
    CHECK(my_strtok(s1, " ") == s1 + 1 && my_strtok(NULL, " ") == s1 + 5 && my_strtok(NULL, " ") == NULL);

    /* Part A — 중첩: static 상태는 안쪽 루프가 바깥 상태를 덮는다 */
    strcpy(buf, "a=1,b=2;c=3,d=4");
    inject(NULL, -1);
    nested_r(buf, out, sizeof out);
    CHECK(STREQ(out, "[a=1|b=2][c=3|d=4]"));
    strcpy(buf, "a=1,b=2;c=3,d=4");
    nested_static(my_strtok, buf, out, sizeof out);
    CHECK(STREQ(out, "[a=1|b=2]"));                           /* 두 번째 그룹이 사라졌다 */

    /* Part B — 스레드: _Thread_local 은 스레드끼리 안전, _r 도 안전 (불안전한 static 판은 demo_strtok_race.c) */
    CHECK(run_threads(1) == 0);
    CHECK(run_threads(0) == 0);
    /* Part B — 그러나 _Thread_local 은 재진입이 아니다: 같은 스레드 안의 중첩·ISR 에선 static 과 똑같이 깨진다 */
    strcpy(buf, "a=1,b=2;c=3,d=4");
    nested_static(my_strtok_tls, buf, out, sizeof out);
    CHECK(STREQ(out, "[a=1|b=2]"));
    /* Part B — ISR 이 main 의 2번째 토큰 직후 끼어든다 */
    inject(isr_tok_r, 2);
    main_flat_r(out, sizeof out);
    CHECK(STREQ(out, "m0|m1|m2|m3|") && STREQ(g_isr_out, "x|y|z|") && g_isr_runs == 1);
    g_cur = my_strtok;
    inject(isr_tok_static, 2);
    main_flat_static(my_strtok, out, sizeof out);
    CHECK(STREQ(out, "m0|m1|") && STREQ(g_isr_out, "x|y|z|"));   /* main 은 m2, m3 을 잃었다 */
    g_cur = my_strtok_tls;
    inject(isr_tok_static, 2);
    main_flat_static(my_strtok_tls, out, sizeof out);
    CHECK(STREQ(out, "m0|m1|") && STREQ(g_isr_out, "x|y|z|"));   /* ISR 은 같은 스레드 → TLS 무력 */
    /* Part B — itoa: 정적 버퍼는 두 결과가 같은 주소, ISR 이 main 의 결과를 덮는다 (주어진 코드의 동작) */
    const char *ia = itoa_static(1), *ib = itoa_static(2);
    CHECK(ia == ib && STREQ(ia, "2"));
    const char *mine = itoa_static(123);
    isr_itoa_static();                                        /* main 이 mine 을 쓰기 전에 ISR */
    CHECK(STREQ(mine, "-45") && STREQ(g_isr_num, "-45"));
    char nb[12], ib2[12];
    char *r1 = itoa_r(123, nb, sizeof nb);
    isr_itoa_r();
    CHECK(r1 == nb && STREQ(nb, "123") && STREQ(g_isr_num, "-45"));
    CHECK(itoa_r(INT_MIN, nb, sizeof nb) == nb && STREQ(nb, "-2147483648"));
    CHECK(itoa_r(0, nb, 2) == nb && STREQ(nb, "0") && itoa_r(-7, ib2, 2) == NULL && ib2[0] == '\0');
    CHECK(itoa_r(INT_MAX, nb, 11) == nb && STREQ(nb, "2147483647") && itoa_r(INT_MAX, nb, 10) == NULL);

    /* Part C — CLI */
    inject(NULL, -1);
    strcpy(buf, "led 1 on; pwm ch=2,duty=50 ;log");
    CHECK(cli_exec(buf, out, sizeof out) == 3 && STREQ(out, "led(1|on) pwm(ch:2|duty:50) log()"));
    strcpy(buf, " ;; \t;");
    CHECK(cli_exec(buf, out, sizeof out) == 0 && STREQ(out, ""));
    strcpy(buf, "cfg\tbaud=115200,parity=N mode=rs485");
    CHECK(cli_exec(buf, out, sizeof out) == 1 && STREQ(out, "cfg(baud:115200|parity:N|mode:rs485)"));
    strcpy(buf, "set a==1,,b");
    CHECK(cli_exec(buf, out, sizeof out) == 1 && STREQ(out, "set(a:1|b)"));   /* strtok 계열은 빈 필드를 합친다 */
    /* Part C — 모든 토큰 경계에 ISR 을 하나씩 주입해도 결과 동일 */
    const char *cmd = "led 1 on; pwm ch=2,duty=50 ;log; cfg baud=9600";
    const char *want = "led(1|on) pwm(ch:2|duty:50) log() cfg(baud:9600)";
    inject(NULL, -1);
    strcpy(buf, cmd);
    cli_exec(buf, out, sizeof out);
    int calls = g_ncall, bad = 0, fired = 0;
    for (int at = 1; at <= calls; at++) {
        inject(telemetry_isr, at);
        strcpy(buf, cmd);
        g_tel_out[0] = '\0';
        int nc = cli_exec(buf, out, sizeof out);
        fired += g_isr_runs;
        if (nc != 4 || !STREQ(out, want) || !STREQ(g_tel_out, "id/7/rate/10/-273")) bad++;
    }
    CHECK(calls >= 20 && fired == calls && bad == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
