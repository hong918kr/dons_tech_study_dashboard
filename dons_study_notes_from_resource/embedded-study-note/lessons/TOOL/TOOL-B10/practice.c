/* TOOL-B10 추가 연습 — lldb 로 찾은 "두 번째 버그" 들을 고친 버전 + 리스트 면접 단골
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 *   연습 1 list_nth (off-by-one)   연습 2 list_insert_sorted (NULL 검사 누락)
 *   연습 3 list_delete_all_pp (prev 없는 포인터-의-포인터 삭제)   연습 4 list_has_cycle (Floyd, O(1) 메모리)
 *   버그 원본: demo_practice_bug.c (lldb 세션: demo_practice.lldb)
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


typedef struct {
    node_t *head;
    size_t len;
} list_t;

static bool list_push_back(list_t *l, int value) {
    node_t *n = node_alloc(value);
    if (n == NULL)
        return false;
    node_t **pp = &l->head;
    while (*pp != NULL)
        pp = &(*pp)->next;
    *pp = n;
    l->len++;
    return true;
}

static void list_free(list_t *l) {
    /* Part B 정답 (solution.c 와 같음) */
    for (node_t *n = l->head, *next; n != NULL; n = next) {
        next = n->next;                             /* 해제하기 전에 저장 */
        node_free(n);
    }
    l->head = NULL;                                 /* 해제한 노드를 가리키는 포인터를 남기지 않는다 */
    l->len = 0;
}

/* 연습 1: idx 번째(0부터) 노드, 범위 밖이면 NULL. 버그 원본은 i <= idx 로 한 칸 더 갔다. */
static node_t *list_nth(const list_t *l, size_t idx) {
    node_t *n = l->head;
    for (size_t i = 0; i < idx && n != NULL; i++)
        n = n->next;
    return n;
}

/* 연습 2: 오름차순 유지 삽입. 끝(NULL)·빈 리스트·맨 앞을 한 루프로: 포인터-의-포인터 */
static bool list_insert_sorted(list_t *l, int value) {
    node_t *n = node_alloc(value);
    if (n == NULL)
        return false;
    node_t **pp = &l->head;
    while (*pp != NULL && (*pp)->value < value)     /* NULL 검사가 먼저 (단락 평가) */
        pp = &(*pp)->next;
    n->next = *pp;
    *pp = n;
    l->len++;
    return true;
}

/* 연습 3: prev 없이 삭제. pp = "지금 노드를 가리키는 링크"의 주소 */
static size_t list_delete_all_pp(list_t *l, int value) {
    size_t removed = 0;
    node_t **pp = &l->head;
    while (*pp != NULL) {
        node_t *cur = *pp;
        if (cur->value == value) {
            *pp = cur->next;                        /* 링크를 먼저 고치고 */
            node_free(cur);                         /* 그 다음 해제. pp 는 그대로 */
            l->len--;
            removed++;
        } else {
            pp = &cur->next;
        }
    }
    return removed;
}

/* 연습 4: 사이클이 있으면 true. 토끼(2칸)와 거북이(1칸)가 만나면 사이클 */
static bool list_has_cycle(const list_t *l) {
    const node_t *slow = l->head, *fast = l->head;
    while (fast != NULL && fast->next != NULL) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast)
            return true;
    }
    return false;
}

/* ---------------- 테스트 하네스 (starter.c 와 같은 방식) ---------------- */
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

static void t_nth(void) {
    list_t l = MAKE(10, 20, 30);
    long sum = 0;
    for (size_t i = 0; i < l.len; i++)
        sum += list_nth(&l, i)->value;              /* 버그 원본은 여기서 NULL 역참조 */
    CHECK(sum == 60);
    CHECK(list_nth(&l, 0) == l.head);
    CHECK(list_nth(&l, 2) != NULL && list_nth(&l, 2)->next == NULL);
    CHECK(list_nth(&l, 3) == NULL && list_nth(&l, 100) == NULL);
    list_t e = {NULL, 0};
    CHECK(list_nth(&e, 0) == NULL);
}
static void t_insert_sorted(void) {
    list_t l = {NULL, 0};
    CHECK(list_insert_sorted(&l, 20));              /* 빈 리스트 */
    CHECK(list_insert_sorted(&l, 40));              /* 맨 뒤: 버그 원본이 죽던 경우 */
    CHECK(list_insert_sorted(&l, 10));              /* 맨 앞 */
    CHECK(list_insert_sorted(&l, 30));              /* 가운데 */
    CHECK(list_insert_sorted(&l, 30));              /* 같은 값 */
    CHECK(EQ(l, 10, 20, 30, 30, 40));
}
static void t_delete_all_pp(void) {
    list_t l = MAKE(5, 1, 5, 5, 2, 5);
    CHECK(list_delete_all_pp(&l, 5) == 4);
    CHECK(EQ(l, 1, 2));
    CHECK(g_live_count == 2 && g_bad_free == 0);
    list_t m = MAKE(3, 3, 3);
    CHECK(list_delete_all_pp(&m, 3) == 3 && m.head == NULL && m.len == 0);
    list_t n = MAKE(1, 2);
    CHECK(list_delete_all_pp(&n, 9) == 0 && EQ(n, 1, 2));
    list_free(&l);
    list_free(&n);
    CHECK(g_live_count == 0 && g_bad_free == 0);
}
static void t_has_cycle(void) {
    list_t e = {NULL, 0};
    CHECK(!list_has_cycle(&e));
    list_t one = MAKE(1);
    CHECK(!list_has_cycle(&one));
    one.head->next = one.head;
    CHECK(list_has_cycle(&one));
    list_t l = MAKE(1, 2, 3, 4, 5);
    CHECK(!list_has_cycle(&l));
    list_nth(&l, 4)->next = list_nth(&l, 2);        /* 5 -> 3 */
    CHECK(list_has_cycle(&l));
    list_t two = MAKE(1, 2);
    list_nth(&two, 1)->next = two.head;             /* 2 -> 1 */
    CHECK(list_has_cycle(&two));
}

static const struct { const char *name; void (*fn)(void); } TESTS[] = {
    {"nth", t_nth}, {"insert_sorted", t_insert_sorted},
    {"delete_all_pp", t_delete_all_pp}, {"has_cycle", t_has_cycle},
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
