/* DS-B14 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ALPHA  37
#define POOL_N 128
#define MAXLEN 16

typedef struct {
    uint8_t child[ALPHA];
    uint8_t n_words;
    int8_t  cmd;
} TNode;

static TNode   g_node[POOL_N];
static uint8_t g_freelist[POOL_N];   /* 연습 2: 반납된 노드 인덱스 스택 */
static int     g_nfree, g_next, g_live;

/* 연습 1: 대소문자 무시 — 매핑 단계에서 접는다(fold). 트라이 크기는 그대로. */
static int char_index(char c) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c >= '0' && c <= '9') return c - '0';
    if (c == '_') return 10;
    if (c >= 'a' && c <= 'z') return 11 + (c - 'a');
    return -1;
}

/* 연습 2: free list 가 있는 풀 — 반납된 노드를 먼저 재사용 */
static int node_new(void) {
    int i;
    if (g_nfree > 0) i = g_freelist[--g_nfree];
    else if (g_next < POOL_N) i = g_next++;
    else return 0;
    memset(g_node[i].child, 0, ALPHA);
    g_node[i].n_words = 0;
    g_node[i].cmd = -1;
    g_live++;
    return i;
}
static void node_free(int i) { g_freelist[g_nfree++] = (uint8_t)i; g_live--; }
static void trie_init(void) { g_nfree = 0; g_next = 0; g_live = 0; node_new(); }

static int nodes_free(void) { return g_nfree + (POOL_N - g_next); }

static bool trie_insert(const char *w, int id) {
    size_t len = strlen(w);
    if (len == 0 || len >= MAXLEN) return false;
    for (size_t i = 0; i < len; i++) if (char_index(w[i]) < 0) return false;
    if (nodes_free() < (int)len) return false;
    int n = 0;
    for (size_t i = 0; i < len; i++) {
        int c = char_index(w[i]);
        if (!g_node[n].child[c]) g_node[n].child[c] = (uint8_t)node_new();
        n = g_node[n].child[c];
    }
    if (g_node[n].cmd >= 0) return false;
    g_node[n].cmd = (int8_t)id;
    n = 0;
    g_node[0].n_words++;
    for (size_t i = 0; i < len; i++) { n = g_node[n].child[char_index(w[i])]; g_node[n].n_words++; }
    return true;
}
static int trie_walk(const char *s) {
    int n = 0;
    for (; *s; s++) {
        int c = char_index(*s);
        if (c < 0 || !g_node[n].child[c]) return -1;
        n = g_node[n].child[c];
    }
    return n;
}
static int trie_exact(const char *s) { int n = trie_walk(s); return n < 0 ? -1 : g_node[n].cmd; }

typedef enum { R_NONE, R_EXACT, R_UNIQUE, R_AMBIG } Res;
static Res trie_resolve(const char *in, int *id) {
    *id = -1;
    if (!in[0]) return R_NONE;
    int n = trie_walk(in);
    if (n < 0) return R_NONE;
    if (g_node[n].cmd >= 0) { *id = g_node[n].cmd; return R_EXACT; }
    if (g_node[n].n_words > 1) return R_AMBIG;
    while (g_node[n].cmd < 0) {
        int c = 0;
        while (!g_node[n].child[c] || !g_node[g_node[n].child[c]].n_words) c++;
        n = g_node[n].child[c];
    }
    *id = g_node[n].cmd;
    return R_UNIQUE;
}

/* 연습 2: 명령어 제거 — n_words 가 0 이 된 노드를 아래에서부터 떼어 풀에 반납 */
static bool trie_remove(const char *w) {
    int path[MAXLEN + 1], cidx[MAXLEN], d = 0, n = 0;
    path[0] = 0;
    for (const char *p = w; *p; p++) {
        int c = char_index(*p);
        if (c < 0 || !g_node[n].child[c] || d >= MAXLEN - 1) return false;
        cidx[d] = c;
        n = g_node[n].child[c];
        path[++d] = n;
    }
    if (g_node[n].cmd < 0) return false;         /* 그런 명령어 없음 (접두사일 뿐) */
    g_node[n].cmd = -1;
    for (int i = 0; i <= d; i++) g_node[path[i]].n_words--;
    for (int i = d; i > 0 && g_node[path[i]].n_words == 0; i--) {   /* 루트는 남긴다 */
        g_node[path[i - 1]].child[cidx[i - 1]] = 0;
        node_free(path[i]);
    }
    return true;
}

/* 연습 3: 트라이 없이 — 플래시의 정렬된 const 배열 + 이진 탐색 (RAM 0바이트) */
static const char *const SORTED[] = {
    "gpio", "help", "i2c_read", "i2c_scan", "i2c_write", "log", "loglevel", "read",
    "reboot", "reset", "sleep", "status", "verbose", "version", "write",
};
enum { NS = (int)(sizeof SORTED / sizeof SORTED[0]) };
static bool starts_with(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
static Res table_resolve(const char *in, int *idx) {
    *idx = -1;
    if (!in[0]) return R_NONE;
    int lo = 0, hi = NS;                         /* lower_bound: 첫 SORTED[i] >= in */
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (strcmp(SORTED[mid], in) < 0) lo = mid + 1; else hi = mid;
    }
    if (lo < NS && strcmp(SORTED[lo], in) == 0) { *idx = lo; return R_EXACT; }
    int k = 0;                                   /* 접두사가 같은 항목은 lo 부터 연속 */
    while (lo + k < NS && starts_with(SORTED[lo + k], in)) k++;
    if (k == 0) return R_NONE;
    if (k > 1) return R_AMBIG;
    *idx = lo;
    return R_UNIQUE;
}

/* 연습 4: 한 줄 → argv (제자리 분할, malloc 없음) → 축약 해석 → 핸들러 호출 */
#define MAX_ARGS 4
typedef int (*Handler)(int argc, char **argv);
static int g_called = -1, g_argc, g_loglevel;
static int h_generic(int argc, char **argv) { (void)argv; g_argc = argc; return 0; }
static int h_loglevel(int argc, char **argv) {
    g_argc = argc;
    if (argc != 2) return -1;
    int v = 0;
    for (const char *p = argv[1]; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        v = v * 10 + (*p - '0');
        if (v > 7) return -1;                    /* 0..7 만 허용 */
    }
    g_loglevel = v;
    return 0;
}
static const char *const CMDS[] = {
    "help", "status", "sleep", "reset", "reboot", "read", "write", "log",
    "loglevel", "i2c_read", "i2c_write", "i2c_scan", "gpio", "version", "verbose",
};
enum { NCMD = (int)(sizeof CMDS / sizeof CMDS[0]), LOGLEVEL_ID = 8 };

static int split_args(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ') *p++ = '\0';           /* 공백을 '\0' 로 바꿔 토큰을 끊는다 */
        if (!*p) break;
        if (argc == MAX_ARGS) return -1;         /* 인자 너무 많음 */
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    return argc;
}
/* 반환: 핸들러 결과, 해석 실패 -2, 빈 줄/인자 과다 -3 */
static int shell_exec(char *line) {
    char *argv[MAX_ARGS];
    int argc = split_args(line, argv), id;
    if (argc <= 0) return -3;
    Res r = trie_resolve(argv[0], &id);
    if (r != R_EXACT && r != R_UNIQUE) return -2;
    g_called = id;
    Handler h = id == LOGLEVEL_ID ? h_loglevel : h_generic;
    return h(argc, argv);
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    trie_init();
    for (int i = 0; i < NCMD; i++) trie_insert(CMDS[i], i);
    int id;

    /* 연습 1 */
    CHECK(trie_exact("STATUS") == 1 && trie_exact("Status") == 1);
    CHECK(trie_resolve("LogL", &id) == R_UNIQUE && id == 8);
    CHECK(trie_insert("HELP", 99) == false);                   /* 대소문자만 다른 중복 */

    /* 연습 2 */
    int live0 = g_live;
    CHECK(trie_remove("loglevel") && trie_exact("loglevel") == -1);
    CHECK(g_live == live0 - 5);                                /* "level" 5개 노드 반납 */
    CHECK(trie_resolve("logl", &id) == R_NONE);
    CHECK(trie_resolve("lo", &id) == R_UNIQUE && id == 7);     /* 이제 lo → log 유일 */
    CHECK(trie_remove("lo") == false && trie_remove("nope") == false);
    CHECK(trie_remove("log") && g_live == live0 - 8);          /* l,o,g 까지 전부 반납 */
    CHECK(trie_insert("loglevel", 8) && trie_insert("log", 7) && g_live == live0);  /* 재사용 */
    CHECK(trie_resolve("logl", &id) == R_UNIQUE && id == 8);

    /* 연습 3 — 모든 명령어의 모든 접두사 + 잡음에 대해 트라이와 같은 판정인지 교차 검증 */
    bool same = true;
    for (int i = 0; i < NCMD; i++) {
        char p[MAXLEN];
        for (size_t L = 1; L <= strlen(CMDS[i]); L++) {
            memcpy(p, CMDS[i], L); p[L] = '\0';
            int a, b;
            Res ra = trie_resolve(p, &a), rb = table_resolve(p, &b);
            if (ra != rb || (rb != R_AMBIG && rb != R_NONE && strcmp(CMDS[a], SORTED[b]) != 0)) same = false;
        }
    }
    static const char *const JUNK[] = {"x", "", "statusx", "i2c", "re", "zz", "g"};
    for (int i = 0; i < 7; i++) {
        int a, b;
        if (trie_resolve(JUNK[i], &a) != table_resolve(JUNK[i], &b)) same = false;
    }
    CHECK(same);
    CHECK(table_resolve("st", &id) == R_UNIQUE && strcmp(SORTED[id], "status") == 0);

    /* 연습 4 */
    char l1[] = "  i2c_s   1 ", l2[] = "logl 5", l3[] = "logl 9", l4[] = "re 0", l5[] = "   ";
    char l6[] = "st a b c d";
    CHECK(shell_exec(l1) == 0 && g_called == 11 && g_argc == 2);
    CHECK(shell_exec(l2) == 0 && g_called == 8 && g_loglevel == 5);
    CHECK(shell_exec(l3) == -1 && g_loglevel == 5);           /* 범위 밖 → 상태 유지 */
    CHECK(shell_exec(l4) == -2);                               /* 모호 */
    CHECK(shell_exec(l5) == -3 && shell_exec(l6) == -3);       /* 빈 줄, 인자 과다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
