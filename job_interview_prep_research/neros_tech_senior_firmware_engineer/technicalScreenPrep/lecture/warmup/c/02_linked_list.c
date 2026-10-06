/* 02 · 연결 리스트 — push_front · length · reverse
 * prompt: node_t {int val; next}. push_front(head, node) → 새 head, list_len(head), reverse(head) → 새 head. malloc 없이 노드를 받아서.
 * note: reverse = prev · curr · next 세 포인터: next 기억 → 화살표 뒤집기 → 한 칸 전진
 * note: 테스트 포인트 — 빈 리스트(NULL), 노드 1개, 뒤집은 뒤 마지막 노드의 next 가 NULL 인지
 */
// --- solution ---
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

typedef struct node {
    int val;
    struct node *next;
} node_t;

node_t *push_front(node_t *head, node_t *n)
{
    n->next = head;
    return n;
}

size_t list_len(const node_t *head)
{
    size_t n = 0;
    for (; head; head = head->next)
        n++;
    return n;
}

node_t *reverse(node_t *head)
{
    node_t *prev = NULL;
    while (head) {
        node_t *next = head->next;
        head->next = prev;
        prev = head;
        head = next;
    }
    return prev;
}

static void test_list(void)
{
    node_t a = {1, NULL}, b = {2, NULL};
    node_t *h = push_front(push_front(NULL, &a), &b);     /* 2 → 1 */
    assert(list_len(h) == 2 && h->val == 2);
    h = reverse(h);                                        /* 1 → 2 */
    assert(h == &a && a.next == &b && b.next == NULL);
    assert(reverse(NULL) == NULL);
}

int main(void)
{
    test_list();
    puts("PASS");
    return 0;
}
// --- end ---
