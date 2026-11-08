/* C-M06 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------- 연습 1: 선언 해독 — 날것 선언 ↔ typedef ---------- */
typedef bool (*parse_fn)(const char *s, uint32_t *out);
static bool parse_dec(const char *s, uint32_t *out) {
    char *e; unsigned long v = strtoul(s, &e, 10);
    if (*s == '\0' || *e != '\0' || v > UINT32_MAX) return false;
    *out = (uint32_t)v; return true;
}
static bool parse_hex(const char *s, uint32_t *out) {
    char *e; unsigned long v = strtoul(s, &e, 16);
    if (*s == '\0' || *e != '\0' || v > UINT32_MAX) return false;
    *out = (uint32_t)v; return true;
}
/* (a) "char 를 받아, (const char *, uint32_t *) 를 받아 bool 을 반환하는 함수를 가리키는 포인터를 반환하는 함수" */
static bool (*pick_parser(char fmt))(const char *, uint32_t *) {
    return fmt == 'x' ? parse_hex : fmt == 'd' ? parse_dec : NULL;
}
/* (b) "parse_fn 2개짜리 배열을 가리키는 포인터" — typedef 없이 */
static parse_fn k_parsers[2] = { parse_dec, parse_hex };
static bool (*(*p_parsers)[2])(const char *, uint32_t *) = &k_parsers;

/* ---------- 연습 2: 정렬된 명령 표 + bsearch ---------- */
typedef int (*cmd_fn)(int argc, char *argv[], void *ctx);
typedef struct { const char *name; cmd_fn fn; } cmd_t;
static int c_ok(int argc, char *argv[], void *ctx) { (void)argv; (void)ctx; return argc; }
static const cmd_t k_sorted[] = {                        /* 이름 오름차순 (strcmp 기준) */
    { "echo", c_ok }, { "help", c_ok }, { "i2c", c_ok }, { "peek", c_ok },
    { "poke", c_ok }, { "reboot", c_ok }, { "spi", c_ok },
};
static const cmd_t k_unsorted[] = { { "peek", c_ok }, { "echo", c_ok } };
/* bsearch 비교 함수: 시그니처가 정확히 int (*)(const void *, const void *) 여야 한다 (캐스트 금지) */
static int cmd_key_cmp(const void *key, const void *elem) {
    const char *name = key;
    const cmd_t *c = elem;
    return strcmp(name, c->name);
}
static bool table_is_sorted(const cmd_t *t, size_t n) {   /* 부팅 시(또는 테스트에서) 한 번 검사 */
    for (size_t i = 1; i < n; i++)
        if (strcmp(t[i - 1].name, t[i].name) >= 0) return false;   /* 중복도 거부 */
    return true;
}
static const cmd_t *find_sorted(const cmd_t *t, size_t n, const char *name) {
    return bsearch(name, t, n, sizeof t[0], cmd_key_cmp);
}

/* ---------- 연습 3: 따옴표를 지원하는 토크나이저 ---------- */
/* "..." 안의 공백은 토큰의 일부. 따옴표 자체는 지운다. 닫히지 않은 따옴표 → -1, 토큰 초과 → -2 */
static int tokenize_q(char *line, char *argv[], int max) {
    int argc = 0;
    char *r = line, *w;
    for (;;) {
        while (*r == ' ' || *r == '\t') r++;
        if (*r == '\0') break;
        if (argc == max) return -2;
        argv[argc++] = w = r;                     /* w: 쓰기 위치 (따옴표를 지우며 앞으로 당긴다) */
        bool inq = false;
        while (*r != '\0' && (inq || (*r != ' ' && *r != '\t'))) {
            if (*r == '"') { inq = !inq; r++; continue; }
            *w++ = *r++;
        }
        if (inq) return -1;
        bool end = (*r == '\0');
        *w = '\0';                                /* w ≤ r 이므로 아직 안 읽은 글자를 덮지 않는다 */
        if (end) break;
        r++;
    }
    argv[argc] = NULL;
    return argc;
}

/* ---------- 연습 4: 이벤트 버스 (콜백 + ctx 구독) ---------- */
typedef void (*evt_cb)(int evt, void *ctx);
#define MAX_SUBS 4
typedef struct { evt_cb fn; void *ctx; } sub_t;
static sub_t g_subs[MAX_SUBS];
static int subscribe(evt_cb fn, void *ctx) {
    if (fn == NULL) return -1;
    for (int i = 0; i < MAX_SUBS; i++)
        if (g_subs[i].fn == fn && g_subs[i].ctx == ctx) return -2;   /* 같은 (fn, ctx) 중복 */
    for (int i = 0; i < MAX_SUBS; i++)
        if (g_subs[i].fn == NULL) { g_subs[i] = (sub_t){ fn, ctx }; return 0; }
    return -3;                                                       /* 가득 참 */
}
static int unsubscribe(evt_cb fn, void *ctx) {
    for (int i = 0; i < MAX_SUBS; i++)
        if (g_subs[i].fn == fn && g_subs[i].ctx == ctx) { g_subs[i] = (sub_t){ NULL, NULL }; return 0; }
    return -1;
}
static int publish(int evt) {
    int n = 0;
    for (int i = 0; i < MAX_SUBS; i++) {
        sub_t s = g_subs[i];                      /* 복사본으로 호출 (콜백이 자기를 해지해도 안전) */
        if (s.fn != NULL) { s.fn(evt, s.ctx); n++; }
    }
    return n;
}
typedef struct { int pin; int toggles; int last_evt; } led_t;
static void led_on_evt(int evt, void *ctx) { led_t *l = ctx; l->toggles++; l->last_evt = evt; }
static int g_log_count;
static void log_on_evt(int evt, void *ctx) { (void)evt; (void)ctx; g_log_count++; }
static void once_on_evt(int evt, void *ctx) { (void)evt; (*(int *)ctx)++; unsubscribe(once_on_evt, ctx); }

/* ---------- 연습 5: ctx 가 있는 정렬 (qsort 에는 ctx 가 없다) ---------- */
typedef int (*cmp_r_fn)(const void *a, const void *b, void *ctx);
static void isort_r(void *base, size_t n, size_t sz, cmp_r_fn cmp, void *ctx) {
    unsigned char *b = base, tmp[32];
    if (sz > sizeof tmp) return;                  /* 데모용 제한 */
    for (size_t i = 1; i < n; i++) {
        memcpy(tmp, b + i * sz, sz);
        size_t j = i;
        while (j > 0 && cmp(b + (j - 1) * sz, tmp, ctx) > 0) {
            memcpy(b + j * sz, b + (j - 1) * sz, sz);
            j--;
        }
        memcpy(b + j * sz, tmp, sz);
    }
}
typedef struct { const uint32_t *val; bool desc; } by_val_ctx_t;
static int cmp_idx_by_val(const void *a, const void *b, void *ctx) {   /* 인덱스를 "값" 기준으로 정렬 */
    const by_val_ctx_t *c = ctx;
    uint32_t va = c->val[*(const uint8_t *)a], vb = c->val[*(const uint8_t *)b];
    int r = (va > vb) - (va < vb);                /* 뺄셈 금지: 오버플로 */
    return c->desc ? -r : r;
}

int main(void) {
    /* 연습 1 */
    CHECK(_Generic(pick_parser, parse_fn (*)(char): 1, default: 0));
    CHECK(_Generic(p_parsers, parse_fn (*)[2]: 1, default: 0));
    uint32_t v = 0;
    parse_fn px = pick_parser('x');
    CHECK(px != NULL && px("1F", &v) && v == 0x1F);
    CHECK(pick_parser('q') == NULL);
    CHECK((*p_parsers)[0]("42", &v) && v == 42);              /* 배열 포인터 → 배열 → 원소 → 호출 */
    CHECK(!(*p_parsers)[1]("zz", &v));

    /* 연습 2 */
    size_t ns = sizeof k_sorted / sizeof k_sorted[0];
    CHECK(table_is_sorted(k_sorted, ns));
    CHECK(!table_is_sorted(k_unsorted, 2));
    CHECK(find_sorted(k_sorted, ns, "poke") == &k_sorted[4]);
    CHECK(find_sorted(k_sorted, ns, "echo") == &k_sorted[0] && find_sorted(k_sorted, ns, "spi") == &k_sorted[6]);
    CHECK(find_sorted(k_sorted, ns, "pok") == NULL && find_sorted(k_sorted, ns, "zzz") == NULL);
    int all = 1;
    for (size_t i = 0; i < ns; i++) all &= (find_sorted(k_sorted, ns, k_sorted[i].name) == &k_sorted[i]);
    CHECK(all);

    /* 연습 3 */
    char *av[6];
    char l1[] = "echo \"hello world\" x";
    CHECK(tokenize_q(l1, av, 5) == 3 && strcmp(av[1], "hello world") == 0 && strcmp(av[2], "x") == 0 && av[3] == NULL);
    char l2[] = "set name=\"a b\"c  \"\"";
    CHECK(tokenize_q(l2, av, 5) == 3 && strcmp(av[1], "name=a bc") == 0 && strcmp(av[2], "") == 0);
    char l3[] = "echo \"oops";
    CHECK(tokenize_q(l3, av, 5) == -1);
    char l4[] = "a b c d e f";
    CHECK(tokenize_q(l4, av, 5) == -2);

    /* 연습 4 */
    led_t led_a = { .pin = 5 }, led_b = { .pin = 6 };
    int once_hits = 0;
    CHECK(subscribe(led_on_evt, &led_a) == 0 && subscribe(led_on_evt, &led_b) == 0);   /* 같은 fn, 다른 ctx */
    CHECK(subscribe(led_on_evt, &led_a) == -2);
    CHECK(subscribe(log_on_evt, NULL) == 0 && subscribe(once_on_evt, &once_hits) == 0);
    CHECK(subscribe(log_on_evt, &led_a) == -3 && subscribe(NULL, NULL) == -1);
    CHECK(publish(7) == 4 && led_a.toggles == 1 && led_b.toggles == 1 && led_a.last_evt == 7);
    CHECK(publish(8) == 3 && once_hits == 1 && g_log_count == 2);                       /* once 는 스스로 해지 */
    CHECK(unsubscribe(led_on_evt, &led_a) == 0 && unsubscribe(led_on_evt, &led_a) == -1);
    CHECK(publish(9) == 2 && led_a.toggles == 2 && led_b.toggles == 3 && led_b.last_evt == 9);

    /* 연습 5 */
    const uint32_t regval[5] = { 30, 0xFFFFFFFFu, 10, 20, 0 };
    uint8_t idx[5] = { 0, 1, 2, 3, 4 };
    by_val_ctx_t asc = { regval, false }, desc = { regval, true };
    isort_r(idx, 5, 1, cmp_idx_by_val, &asc);
    CHECK(memcmp(idx, (uint8_t[]){ 4, 2, 3, 0, 1 }, 5) == 0);
    isort_r(idx, 5, 1, cmp_idx_by_val, &desc);
    CHECK(memcmp(idx, (uint8_t[]){ 1, 0, 3, 2, 4 }, 5) == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
