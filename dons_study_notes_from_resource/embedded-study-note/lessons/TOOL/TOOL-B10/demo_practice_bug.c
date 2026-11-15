/* TOOL-B10 demo_practice_bug.c — 추가 연습 1·2 의 "버그 심은" 원본 (lldb 로 찾는 연습용)
 *   cc -std=c11 -g -O0 demo_practice_bug.c -o pbug && lldb -b -s demo_practice.lldb -- ./pbug nth
 *   ./pbug nth     → list_nth off-by-one (크래시는 main 에서 난다)
 *   ./pbug sorted  → list_insert_sorted 의 NULL 검사 누락 (크래시는 list_insert_sorted 안)
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct node { int value; struct node *next; } node_t;
typedef struct { node_t *head; size_t len; } list_t;

static node_t g_nodes[8];
static int g_used;

static void push_back(list_t *l, int v) {
    node_t *n = &g_nodes[g_used++];
    n->value = v;
    n->next = NULL;
    node_t **pp = &l->head;
    while (*pp != NULL)
        pp = &(*pp)->next;
    *pp = n;
    l->len++;
}

/* idx 번째(0부터) 노드. 범위 밖이면 NULL */
static node_t *list_nth(const list_t *l, size_t idx) {
    node_t *n = l->head;
    for (size_t i = 0; i <= idx && n != NULL; i++)
        n = n->next;
    return n;
}

/* 오름차순 리스트에 v 를 끼워 넣는다 */
static void list_insert_sorted(list_t *l, node_t *nn) {
    if (l->head == NULL || nn->value < l->head->value) {
        nn->next = l->head;
        l->head = nn;
    } else {
        node_t *cur = l->head;
        while (cur->next->value < nn->value)
            cur = cur->next;
        nn->next = cur->next;
        cur->next = nn;
    }
    l->len++;
}

int main(int argc, char **argv) {
    list_t l = {NULL, 0};
    push_back(&l, 10);
    push_back(&l, 20);
    push_back(&l, 30);
    if (argc == 2 && strcmp(argv[1], "nth") == 0) {
        long sum = 0;
        for (size_t i = 0; i < l.len; i++)
            sum += list_nth(&l, i)->value;      /* 10+20+30 = 60 이어야 한다 */
        printf("sum = %ld\n", sum);
    } else if (argc == 2 && strcmp(argv[1], "sorted") == 0) {
        node_t *nn = &g_nodes[g_used++];
        nn->value = 40;
        nn->next = NULL;
        list_insert_sorted(&l, nn);              /* 맨 뒤에 들어가야 한다 */
        printf("len = %zu\n", l.len);
    }
    return 0;
}
