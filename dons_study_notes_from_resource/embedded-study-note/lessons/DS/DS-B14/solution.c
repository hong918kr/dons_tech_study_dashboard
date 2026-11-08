/* DS-B14 트라이(Trie): 명령어 파서와 자동완성
 * 디버그 셸 명령어를 정적 노드 풀 트라이에 넣는다 (malloc 없음).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ALPHA  37   /* '0'-'9' (10) + '_' (1) + 'a'-'z' (26) */
#define POOL_N 128  /* uint8_t 인덱스로 가리킬 수 있게 ≤ 255 */
#define MAXLEN 16   /* 명령어 최대 길이 15 + '\0' */

typedef struct {
    uint8_t child[ALPHA];  /* 자식 노드 인덱스, 0 = 없음 (0번은 루트라 자식이 될 수 없다) */
    uint8_t n_words;       /* 이 노드 아래(자신 포함)에서 끝나는 명령어 수 */
    int8_t  cmd;           /* 여기서 끝나는 명령어 id, 없으면 -1 */
} TNode;

/* ---------------- 정적 노드 풀 + 문자 매핑 (주어짐) ---------------- */
static TNode g_node[POOL_N];
static int   g_used;       /* 사용한 노드 수 (루트 포함) */

/* 인덱스 순서 = ASCII 순서 ('0'..'9' < '_' < 'a'..'z') → DFS 하면 사전순 */
static int char_index(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c == '_') return 10;
    if (c >= 'a' && c <= 'z') return 11 + (c - 'a');
    return -1;
}
static char index_char(int i) {
    return i < 10 ? (char)('0' + i) : i == 10 ? '_' : (char)('a' + (i - 11));
}
static int node_new(void) {
    if (g_used >= POOL_N) return 0;
    TNode *n = &g_node[g_used];
    memset(n->child, 0, sizeof n->child);
    n->n_words = 0;
    n->cmd = -1;
    return g_used++;
}
static void trie_init(void) {
    g_used = 0;
    node_new();            /* 0번 = 루트 */
}

/* ---------------- Part A: 삽입 · 경로 따라가기 · 정확 일치 ---------------- */
/* 성공하면 true. 빈 문자열/너무 김/허용 안 되는 문자/중복/풀 부족이면 false 이고
 * 트라이와 풀은 전혀 바뀌지 않는다. */
static bool trie_insert(const char *w, int id) {
    size_t len = strlen(w);
    if (len == 0 || len >= MAXLEN) return false;
    for (size_t i = 0; i < len; i++)               /* ① 검증 먼저 — 노드를 만들기 전에 */
        if (char_index(w[i]) < 0) return false;
    if (POOL_N - g_used < (int)len) return false;  /* ② 최악 필요량을 미리 확인 */
    int n = 0;
    for (size_t i = 0; i < len; i++) {             /* ③ 경로를 만들며 내려간다 */
        int c = char_index(w[i]);
        if (!g_node[n].child[c]) g_node[n].child[c] = (uint8_t)node_new();
        n = g_node[n].child[c];
    }
    if (g_node[n].cmd >= 0) return false;          /* 중복 (이 경우 새 노드는 안 생겼다) */
    g_node[n].cmd = (int8_t)id;
    n = 0;                                         /* ④ 경로의 n_words 를 1씩 올린다 */
    g_node[0].n_words++;
    for (size_t i = 0; i < len; i++) {
        n = g_node[n].child[char_index(w[i])];
        g_node[n].n_words++;
    }
    return true;
}

/* s 를 따라 내려간 노드 인덱스. 경로가 없거나 허용 안 되는 문자면 -1. "" → 루트(0). */
static int trie_walk(const char *s) {
    int n = 0;
    for (; *s; s++) {
        int c = char_index(*s);
        if (c < 0 || !g_node[n].child[c]) return -1;
        n = g_node[n].child[c];
    }
    return n;
}

/* 정확히 일치하는 명령어 id, 없으면 -1 */
static int trie_exact(const char *s) {
    int n = trie_walk(s);
    return n < 0 ? -1 : g_node[n].cmd;
}

/* ---------------- Part B: 고유 접두사 축약 ---------------- */
typedef enum { R_NONE, R_EXACT, R_UNIQUE, R_AMBIG } Res;

/* 정확 일치가 우선("log" → log, loglevel 이 있어도). 아니면 접두사 아래 명령어가
 * 1개면 UNIQUE, 2개 이상이면 AMBIG. 빈 입력·없는 접두사는 NONE. */
static Res trie_resolve(const char *in, int *id) {
    *id = -1;
    if (!in[0]) return R_NONE;
    int n = trie_walk(in);
    if (n < 0) return R_NONE;
    if (g_node[n].cmd >= 0) { *id = g_node[n].cmd; return R_EXACT; }
    if (g_node[n].n_words > 1) return R_AMBIG;
    while (g_node[n].cmd < 0) {                    /* n_words == 1: 외길을 따라 끝까지 */
        int c = 0;
        while (!g_node[n].child[c] || !g_node[g_node[n].child[c]].n_words) c++;
        n = g_node[n].child[c];
    }
    *id = g_node[n].cmd;
    return R_UNIQUE;
}

/* ---------------- Part C: 후보 목록 (탭 자동완성) ---------------- */
/* buf[0..depth) = 지금까지의 글자. 사전순 DFS 로 단어를 out 에 모은다 (max 개까지만 기록). */
static void collect(int n, char *buf, int depth, char out[][MAXLEN], int max, int *cnt) {
    if (g_node[n].cmd >= 0) {                      /* 자신을 먼저 → "log" 가 "loglevel" 앞 */
        if (*cnt < max) { memcpy(out[*cnt], buf, (size_t)depth); out[*cnt][depth] = '\0'; }
        (*cnt)++;
    }
    for (int c = 0; c < ALPHA; c++) {
        if (!g_node[n].child[c]) continue;
        buf[depth] = index_char(c);
        collect(g_node[n].child[c], buf, depth + 1, out, max, cnt);
    }
}
/* 반환값 = 후보 전체 개수 (max 보다 클 수 있음, snprintf 처럼) */
static int trie_complete(const char *prefix, char out[][MAXLEN], int max) {
    int n = trie_walk(prefix);
    if (n < 0) return 0;
    char buf[MAXLEN];
    size_t len = strlen(prefix);
    memcpy(buf, prefix, len);
    int cnt = 0;
    collect(n, buf, (int)len, out, max, &cnt);
    return cnt;
}

/* 탭 한 번: 후보들의 최장 공통 접두사까지 채운다 ("i" → "i2c_"). 경로가 없으면 false. */
static bool trie_lcp(const char *prefix, char out[MAXLEN]) {
    int n = trie_walk(prefix);
    if (n < 0) return false;
    size_t len = strlen(prefix);
    memcpy(out, prefix, len);
    while (g_node[n].cmd < 0) {                    /* 단어 끝이면 멈춤 ("log" 에서 멈춰야 한다) */
        int only = -1, k = 0;
        for (int c = 0; c < ALPHA; c++)
            if (g_node[n].child[c]) { only = c; k++; }
        if (k != 1) break;                         /* 갈림길이면 멈춤 */
        out[len++] = index_char(only);
        n = g_node[n].child[only];
    }
    out[len] = '\0';
    return true;
}

/* ---------------- 셸 한 줄 처리 (주어짐) ---------------- */
static const char *const CMDS[] = {
    "help", "status", "sleep", "reset", "reboot", "read", "write", "log",
    "loglevel", "i2c_read", "i2c_write", "i2c_scan", "gpio", "version", "verbose",
};
enum { NCMD = (int)(sizeof CMDS / sizeof CMDS[0]) };

static void shell_line(const char *line, char *msg, size_t cap) {
    char word[MAXLEN];
    size_t i = 0;
    while (line[i] && line[i] != ' ' && i < MAXLEN - 1) { word[i] = line[i]; i++; }
    word[i] = '\0';
    int id = -1;
    Res r = (line[i] && line[i] != ' ') ? R_NONE : trie_resolve(word, &id);  /* 너무 긴 단어 */
    if (r == R_EXACT || r == R_UNIQUE) {
        snprintf(msg, cap, "run %s", CMDS[id]);
    } else if (r == R_AMBIG) {
        char cand[NCMD][MAXLEN];
        int k = trie_complete(word, cand, NCMD);
        size_t off = (size_t)snprintf(msg, cap, "ambiguous '%s':", word);
        for (int j = 0; j < k && off < cap; j++)
            off += (size_t)snprintf(msg + off, cap - off, " %s", cand[j]);
    } else {
        snprintf(msg, cap, "unknown '%s'", word);
    }
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    trie_init();
    int ok = 0;
    for (int i = 0; i < NCMD; i++) ok += trie_insert(CMDS[i], i);
    printf("  commands=%d  nodes=%d/%d  sizeof(TNode)=%zu  pool bytes=%zu\n",
           NCMD, g_used, POOL_N, sizeof(TNode), sizeof g_node);

    /* Part A — 삽입·정확 일치 */
    CHECK(ok == NCMD);
    int before = g_used;
    CHECK(trie_insert("status", 99) == false);                 /* 중복 */
    CHECK(trie_insert("Bad!", 99) == false);                   /* 허용 안 되는 문자 */
    CHECK(trie_insert("", 99) == false);
    CHECK(trie_insert("abcdefghijklmnop", 99) == false);       /* 16자 > 15 */
    CHECK(g_used == before);                                   /* 실패한 삽입은 노드를 안 먹는다 */
    CHECK(trie_exact("status") == 1 && trie_exact("log") == 7 && trie_exact("loglevel") == 8);
    CHECK(trie_exact("stat") == -1 && trie_exact("logl") == -1);
    CHECK(trie_exact("xyz") == -1 && trie_exact("STATUS") == -1);
    CHECK(trie_walk("i2c_") > 0 && trie_walk("i2d") == -1 && trie_walk("") == 0);

    /* Part B — 고유 접두사 축약 */
    int id = -1;
    CHECK(trie_resolve("st", &id) == R_UNIQUE && id == 1);     /* "st" → status */
    CHECK(trie_resolve("status", &id) == R_EXACT && id == 1);
    CHECK(trie_resolve("s", &id) == R_AMBIG && id == -1);      /* sleep, status */
    CHECK(trie_resolve("re", &id) == R_AMBIG);                 /* read, reboot, reset */
    CHECK(trie_resolve("reb", &id) == R_UNIQUE && id == 4);
    CHECK(trie_resolve("log", &id) == R_EXACT && id == 7);     /* 정확 일치 우선 */
    CHECK(trie_resolve("logl", &id) == R_UNIQUE && id == 8);
    CHECK(trie_resolve("lo", &id) == R_AMBIG);
    CHECK(trie_resolve("i2c_s", &id) == R_UNIQUE && id == 11);
    CHECK(trie_resolve("h", &id) == R_UNIQUE && id == 0);
    CHECK(trie_resolve("x", &id) == R_NONE && trie_resolve("", &id) == R_NONE);
    CHECK(trie_resolve("statusx", &id) == R_NONE);

    char msg[128];
    shell_line("st", msg, sizeof msg);
    CHECK(strcmp(msg, "run status") == 0);
    shell_line("logl 3", msg, sizeof msg);
    CHECK(strcmp(msg, "run loglevel") == 0);
    shell_line("re 0x40", msg, sizeof msg);
    CHECK(strcmp(msg, "ambiguous 're': read reboot reset") == 0);
    shell_line("frob", msg, sizeof msg);
    CHECK(strcmp(msg, "unknown 'frob'") == 0);
    printf("  > re 0x40  -> %s\n", (shell_line("re 0x40", msg, sizeof msg), msg));

    /* Part C — 후보 목록 · 공통 접두사 */
    char cand[NCMD][MAXLEN];
    CHECK(trie_complete("re", cand, NCMD) == 3 && strcmp(cand[0], "read") == 0
          && strcmp(cand[1], "reboot") == 0 && strcmp(cand[2], "reset") == 0);
    CHECK(trie_complete("i2c_", cand, NCMD) == 3 && strcmp(cand[0], "i2c_read") == 0
          && strcmp(cand[1], "i2c_scan") == 0 && strcmp(cand[2], "i2c_write") == 0);
    CHECK(trie_complete("log", cand, NCMD) == 2 && strcmp(cand[0], "log") == 0
          && strcmp(cand[1], "loglevel") == 0);
    CHECK(trie_complete("zz", cand, NCMD) == 0);
    strcpy(cand[4], "SENTINEL");
    CHECK(trie_complete("", cand, 4) == NCMD && strcmp(cand[0], "gpio") == 0
          && strcmp(cand[3], "i2c_scan") == 0 && strcmp(cand[4], "SENTINEL") == 0);
    char buf[MAXLEN];
    CHECK(trie_lcp("i", buf) && strcmp(buf, "i2c_") == 0);
    CHECK(trie_lcp("v", buf) && strcmp(buf, "ver") == 0);
    CHECK(trie_lcp("he", buf) && strcmp(buf, "help") == 0);
    CHECK(trie_lcp("log", buf) && strcmp(buf, "log") == 0);    /* 단어 끝에서 멈춤 */
    CHECK(trie_lcp("re", buf) && strcmp(buf, "re") == 0);      /* 갈림길에서 멈춤 */
    CHECK(trie_lcp("q", buf) == false);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
