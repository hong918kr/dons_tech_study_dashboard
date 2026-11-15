/* TOOL-B10 디버거 기초: breakpoint·step·backtrace·print
 *
 * 단일 연결 리스트(singly linked list) + 디버그용 노드 풀(해제하면 poison 을 채움).
 *   빌드·테스트 : cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
 *                 → 테스트마다 fork() 한 자식에서 실행: 크래시해도 FAIL 한 줄로 끝난다.
 *   디버그      : cc -std=c11 -g -O0 starter.c -o starter_dbg
 *                 lldb -- ./starter_dbg delete_head   → 그 테스트 하나만 fork 없이 실행
 *   ./starter list → 테스트 이름 목록.   -DVERIFY → 변경 함수 끝에서 list_check (Part C)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---------------- 노드 풀 (TODO 아님) ----------------
 * 펌웨어처럼 malloc 대신 고정 크기 풀. 해제한 노드는 재사용하지 않고 poison 으로 덮는다
 * (Linux 의 LIST_POISON / slab 0x6b 와 같은 아이디어) → 해제 후 사용이 "바로" 티가 난다. */
typedef struct node {
    int value;
    struct node *next;
} node_t;

#define POOL_SIZE  256
#define POISON_VAL 0x6B6B6B6B                          /* 해제된 노드의 value */
#define POISON_PTR ((node_t *)(uintptr_t)0xDEAD0000u)  /* 해제된 노드의 next: 매핑 안 된 주소 */

static node_t g_pool[POOL_SIZE];
static bool g_live[POOL_SIZE];
static int g_used, g_live_count, g_bad_free;

static node_t *node_alloc(int value) {
    if (g_used == POOL_SIZE)
        return NULL;
    node_t *n = &g_pool[g_used];
    g_live[g_used++] = true;
    g_live_count++;
    n->value = value;
    n->next = NULL;
    return n;
}

static long pool_index(const node_t *n) {          /* 풀 밖이면 -1 */
    uintptr_t p = (uintptr_t)n, base = (uintptr_t)g_pool;
    if (p < base || p >= base + sizeof g_pool || (p - base) % sizeof(node_t) != 0)
        return -1;
    return (long)((p - base) / sizeof(node_t));
}

static bool node_is_live(const node_t *n) {
    long i = pool_index(n);
    return i >= 0 && i < g_used && g_live[i];
}

static void node_free(node_t *n) {
    if (!node_is_live(n)) {                         /* 이중 해제 · 풀 밖 포인터 */
        g_bad_free++;
        return;
    }
    g_live[pool_index(n)] = false;
    g_live_count--;
    n->value = POISON_VAL;
    n->next = POISON_PTR;
}

/* ---------------- 리스트 ---------------- */
typedef struct {
    node_t *head;
    size_t len;
} list_t;

enum { LC_OK = 0, LC_DANGLING = 1, LC_LEN = 2, LC_CYCLE = 3 };
static int list_check(const list_t *l);

#ifdef VERIFY   /* 디버그 빌드: 리스트를 바꾼 직후 불변식 검사 → 버그 난 함수 안에서 멈춘다 */
#define VERIFY_LIST(l) do { int vc_ = list_check(l); if (vc_ != LC_OK) { \
    printf("VERIFY: list_check=%d in %s\n", vc_, __func__); fflush(stdout); abort(); } } while (0)
#else
#define VERIFY_LIST(l) ((void)0)
#endif

static bool list_push_front(list_t *l, int value) {
    node_t *n = node_alloc(value);
    if (n == NULL)
        return false;
    n->next = l->head;
    l->head = n;
    l->len++;
    VERIFY_LIST(l);
    return true;
}

static bool list_push_back(list_t *l, int value) {
    node_t *n = node_alloc(value);
    if (n == NULL)
        return false;
    node_t **pp = &l->head;
    while (*pp != NULL)
        pp = &(*pp)->next;
    *pp = n;
    l->len++;
    VERIFY_LIST(l);
    return true;
}

static node_t *list_find(const list_t *l, int value) {
    for (node_t *n = l->head; n != NULL; n = n->next)
        if (n->value == value)
            return n;
    return NULL;
}

static long list_sum(const list_t *l) {
    long s = 0;
    for (node_t *n = l->head; n != NULL; n = n->next)
        s += n->value;
    return s;
}

/* Part A: value 와 같은 첫 노드를 지운다. 지웠으면 true. */
static bool list_delete(list_t *l, int value) {
    /* TODO(Part A): 버그 1개. ./starter_dbg delete_head 를 lldb 로 추적해 찾아 고쳐라 */
    node_t *prev = NULL;
    node_t *cur = l->head;
    while (cur != NULL && cur->value != value) {
        prev = cur;
        cur = cur->next;
    }
    if (cur == NULL)
        return false;
    if (prev != NULL)
        prev->next = cur->next;
    else
        l->head = cur->next;                        /* 맨 앞 노드면 head 를 옮긴다 */
    node_free(cur);
    l->len--;
    VERIFY_LIST(l);
    return true;
}

/* Part B: value 와 같은 노드를 모두 지우고 지운 개수를 돌려준다. */
static size_t list_delete_all(list_t *l, int value) {
    /* TODO(Part B): 버그 2개 (하나를 고쳐야 다른 하나가 보인다) */
    size_t removed = 0;
    node_t *prev = NULL;
    for (node_t *cur = l->head, *next; cur != NULL; cur = next) {
        next = cur->next;                           /* 해제하기 전에 다음 노드를 저장 */
        if (cur->value == value) {
            if (prev == NULL)
                l->head = next;
            else
                prev->next = next;
            node_free(cur);
            l->len--;
            removed++;
            continue;                               /* 지웠으면 prev 는 그대로 */
        }
        prev = cur;
    }
    VERIFY_LIST(l);
    return removed;
}

/* Part B: 모든 노드를 해제하고 빈 리스트로 만든다. */
static void list_free(list_t *l) {
    /* TODO(Part B): 버그 2개 */
    for (node_t *n = l->head, *next; n != NULL; n = next) {
        next = n->next;                             /* 해제하기 전에 저장 */
        node_free(n);
    }
    l->head = NULL;                                 /* 해제한 노드를 가리키는 포인터를 남기지 않는다 */
    l->len = 0;
}

/* Part C: 리스트 불변식 검사. 절대 크래시하면 안 된다 (깨진 리스트를 받는 함수다).
 *   LC_DANGLING : head 에서 닿는 노드 중 살아 있지 않은(해제됐거나 풀 밖) 포인터가 있다
 *   LC_CYCLE    : 사이클 (POOL_SIZE 개보다 많이 걸어도 끝이 안 난다)
 *   LC_LEN      : 실제 노드 수 != l->len
 *   검사 순서: 따라가기 전에 DANGLING → CYCLE → 끝까지 간 뒤 LEN */
static int list_check(const list_t *l) {
    /* ==== list_check begin ==== */
    size_t count = 0;
    for (const node_t *n = l->head; n != NULL; n = n->next) {
        if (!node_is_live(n))                       /* 따라가기(n->next) 전에 검사 */
            return LC_DANGLING;
        if (++count > POOL_SIZE)                    /* 살아 있는 노드는 최대 POOL_SIZE 개 */
            return LC_CYCLE;
    }
    return count == l->len ? LC_OK : LC_LEN;
    /* ==== list_check end ==== */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_fail;
#define CHECK(expr) do { if (!(expr)) { g_fail++; \
    printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static bool list_equals(const list_t *l, const int *exp, size_t n) {
    const node_t *p = l->head;
    for (size_t i = 0; i < n; i++, p = p->next)
        if (p == NULL || p->value != exp[i])
            return false;
    return p == NULL && l->len == n;
}

static list_t make_list(const int *v, size_t n) {
    list_t l = {NULL, 0};
    for (size_t i = 0; i < n; i++)
        list_push_back(&l, v[i]);
    return l;
}
#define MAKE(...) make_list((const int[]){__VA_ARGS__}, sizeof((int[]){__VA_ARGS__}) / sizeof(int))
#define EQ(l, ...) list_equals(&(l), (const int[]){__VA_ARGS__}, sizeof((int[]){__VA_ARGS__}) / sizeof(int))

/* Part A */
static void t_basic(void) {
    list_t l = MAKE(1, 2, 3);
    list_push_front(&l, 0);
    CHECK(EQ(l, 0, 1, 2, 3));
    CHECK(list_find(&l, 2) != NULL && list_find(&l, 2)->value == 2);
    CHECK(list_find(&l, 9) == NULL);
    CHECK(list_sum(&l) == 6);
}
static void t_delete_middle(void) {
    list_t l = MAKE(1, 2, 3);
    CHECK(list_delete(&l, 2));
    CHECK(EQ(l, 1, 3));
    CHECK(list_sum(&l) == 4);
}
static void t_delete_tail(void) {
    list_t l = MAKE(1, 2, 3);
    CHECK(list_delete(&l, 3));
    CHECK(EQ(l, 1, 2));
    CHECK(!list_delete(&l, 42) && EQ(l, 1, 2));
}
static void t_delete_head(void) {
    list_t l = MAKE(1, 2, 3);
    CHECK(list_delete(&l, 1));
    CHECK(list_sum(&l) == 5);                       /* 2 + 3 */
    CHECK(EQ(l, 2, 3));
}
static void t_delete_only(void) {
    list_t l = MAKE(7);
    CHECK(list_delete(&l, 7));
    CHECK(l.head == NULL && l.len == 0);
    CHECK(g_live_count == 0);
}
static void t_delete_dup_first(void) {               /* 같은 값이면 첫 번째만 */
    list_t l = MAKE(4, 1, 4);
    CHECK(list_delete(&l, 4));
    CHECK(EQ(l, 1, 4));
}
/* Part B */
static void t_delete_all_none(void) {
    list_t l = MAKE(1, 2, 3);
    CHECK(list_delete_all(&l, 9) == 0);
    CHECK(EQ(l, 1, 2, 3));
}
static void t_delete_all_mixed(void) {
    list_t l = MAKE(5, 1, 5, 5, 2, 5);
    CHECK(list_delete_all(&l, 5) == 4);
    CHECK(EQ(l, 1, 2));
    CHECK(g_live_count == 2);
}
static void t_delete_all_every(void) {
    list_t l = MAKE(3, 3, 3);
    CHECK(list_delete_all(&l, 3) == 3);
    CHECK(l.head == NULL && l.len == 0 && g_live_count == 0);
}
static void t_free(void) {
    list_t l = MAKE(1, 2, 3, 4);
    list_free(&l);
    CHECK(l.head == NULL && l.len == 0);
    CHECK(g_live_count == 0 && g_bad_free == 0);
}
static void t_free_empty(void) {
    list_t l = {NULL, 0};
    list_free(&l);
    CHECK(l.head == NULL && l.len == 0 && g_bad_free == 0);
}
/* Part C: 일부러 망가뜨린 리스트를 list_check 에 넣는다 */
static void t_check_ok(void) {
    list_t l = MAKE(1, 2, 3);
    list_t e = {NULL, 0};
    CHECK(list_check(&l) == LC_OK);
    CHECK(list_check(&e) == LC_OK);
}
static void t_check_dangling(void) {
    list_t l = MAKE(1, 2, 3);
    node_free(l.head->next);                        /* 2 를 해제했지만 1->next 는 그대로 */
    CHECK(list_check(&l) == LC_DANGLING);
    list_t m = MAKE(1, 2);
    node_free(m.head);                              /* head 가 해제된 노드 (Part A 버그 모양) */
    CHECK(list_check(&m) == LC_DANGLING);
    node_t outside = {5, NULL};
    list_t o = {&outside, 1};                       /* 풀 밖 포인터 */
    CHECK(list_check(&o) == LC_DANGLING);
}
static void t_check_len(void) {
    list_t l = MAKE(1, 2, 3);
    l.len = 2;
    CHECK(list_check(&l) == LC_LEN);
    list_t e = {NULL, 1};
    CHECK(list_check(&e) == LC_LEN);
}
static void t_check_cycle(void) {
    list_t l = MAKE(1, 2, 3);
    l.head->next->next->next = l.head->next;        /* 3 -> 2 */
    CHECK(list_check(&l) == LC_CYCLE);
    list_t s = MAKE(8);
    s.head->next = s.head;                          /* 자기 자신 */
    CHECK(list_check(&s) == LC_CYCLE);
}
static void t_random_ops(void) {                    /* 배열 모델과 200 연산 비교 */
    list_t l = {NULL, 0};
    int model[POOL_SIZE];
    size_t mn = 0;
    uint32_t x = 12345u;
    for (int step = 0; step < 200; step++) {
        x = x * 1103515245u + 12345u;
        int op = (int)((x >> 16) % 4u), v = (int)((x >> 8) % 6u);
        if (op == 0 && list_push_back(&l, v)) {
            model[mn++] = v;
        } else if (op == 1 && list_push_front(&l, v)) {
            memmove(model + 1, model, mn * sizeof model[0]);
            model[0] = v;
            mn++;
        } else if (op == 2) {
            size_t i = 0;
            while (i < mn && model[i] != v)
                i++;
            bool had = i < mn;
            if (had)
                memmove(model + i, model + i + 1, (--mn - i) * sizeof model[0]);
            if (list_delete(&l, v) != had) { CHECK(!"list_delete result"); return; }
        } else if (op == 3) {
            size_t k = 0, r;
            for (size_t i = 0; i < mn; i++)
                if (model[i] != v)
                    model[k++] = model[i];
            r = mn - k;
            mn = k;
            if (list_delete_all(&l, v) != r) { CHECK(!"list_delete_all count"); return; }
        }
        if (list_check(&l) != LC_OK || !list_equals(&l, model, mn)) {
            printf("    step %d: op %d value %d\n", step, op, v);
            CHECK(!"list == model");
            return;
        }
    }
    list_free(&l);
    CHECK(g_live_count == 0 && g_bad_free == 0);
}

static const struct { const char *name; void (*fn)(void); } TESTS[] = {
    {"basic", t_basic},               {"delete_middle", t_delete_middle},
    {"delete_tail", t_delete_tail},   {"delete_head", t_delete_head},
    {"delete_only", t_delete_only},   {"delete_dup_first", t_delete_dup_first},
    {"delete_all_none", t_delete_all_none}, {"delete_all_mixed", t_delete_all_mixed},
    {"delete_all_every", t_delete_all_every}, {"free", t_free},
    {"free_empty", t_free_empty},     {"check_ok", t_check_ok},
    {"check_dangling", t_check_dangling}, {"check_len", t_check_len},
    {"check_cycle", t_check_cycle},   {"random_ops", t_random_ops},
};
enum { NTESTS = sizeof TESTS / sizeof TESTS[0] };

static void on_crash(int sig) { _exit(128 + sig); }  /* 크래시 리포트 없이 부모에 신호 번호 전달 */

static int run_in_child(int i) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_crash;
        int sigs[] = {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGTRAP, SIGFPE, SIGALRM};
        for (size_t k = 0; k < sizeof sigs / sizeof sigs[0]; k++)
            sigaction(sigs[k], &sa, NULL);
        alarm(2);                                   /* 무한 루프 방지: 2초면 SIGALRM */
        TESTS[i].fn();
        fflush(stdout);
        _exit(g_fail ? 1 : 0);
    }
    int st = 0;
    pid_t r;
    while ((r = waitpid(pid, &st, 0)) < 0 && errno == EINTR)
        ;                                           /* 디버거 아래에서는 EINTR 이 실제로 온다 */
    if (pid < 0 || r != pid) {
        printf("FAIL %-17s fork/wait error\n", TESTS[i].name);
        return 0;
    }
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
    if (code == 0) {
        printf("PASS %s\n", TESTS[i].name);
        return 1;
    }
    if (code > 128)
        printf("FAIL %-17s crashed: signal %d (%s)\n", TESTS[i].name, code - 128, strsignal(code - 128));
    else
        printf("FAIL %-17s check failed\n", TESTS[i].name);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        for (int i = 0; i < NTESTS; i++)
            printf("%s\n", TESTS[i].name);
        return 0;
    }
    if (argc == 2) {                                /* 디버거용: fork 없이 한 테스트만 */
        for (int i = 0; i < NTESTS; i++)
            if (strcmp(argv[1], TESTS[i].name) == 0) {
                TESTS[i].fn();
                printf("%s: %s\n", TESTS[i].name, g_fail ? "FAIL" : "PASS");
                return g_fail ? 1 : 0;
            }
        printf("unknown test '%s' (try: list)\n", argv[1]);
        return 2;
    }
    int pass = 0;
    for (int i = 0; i < NTESTS; i++)
        pass += run_in_child(i);
    printf("%d/%d passed\n", pass, (int)NTESTS);
    return pass == NTESTS ? 0 : 1;
}
