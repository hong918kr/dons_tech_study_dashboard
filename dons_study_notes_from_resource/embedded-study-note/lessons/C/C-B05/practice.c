/* C-B05 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct node { int val; struct node *next; };

/* 연습 1: val 과 같은 노드를 전부 떼어낸다. 떼어낸 개수 반환.
 * 지웠을 때는 pp 를 전진시키지 않는다 — *pp 가 이미 다음 노드를 가리킴 */
static size_t list_remove_all(struct node **head, int val) {
    size_t n = 0;
    struct node **pp = head;
    while (*pp) {
        if ((*pp)->val == val) { struct node *v = *pp; *pp = v->next; v->next = NULL; n++; }
        else pp = &(*pp)->next;
    }
    return n;
}

/* 연습 2: 오름차순 정렬 유지하며 삽입. "끼울 자리(링크)"를 찾아서 거기에 끼운다 */
static void list_insert_sorted(struct node **head, struct node *n) {
    struct node **pp = head;
    while (*pp && (*pp)->val < n->val) pp = &(*pp)->next;
    n->next = *pp;
    *pp = n;
}

/* 연습 3: 리스트 뒤집기 (head 가 바뀌므로 이중 포인터) */
static void list_reverse(struct node **head) {
    struct node *prev = NULL, *cur = *head;
    while (cur) { struct node *nx = cur->next; cur->next = prev; prev = cur; cur = nx; }
    *head = prev;
}

/* 연습 4: 포인터를 align(2의 거듭제곱) 경계로 올림 — DMA 버퍼 정렬 */
static bool is_aligned(const void *p, size_t align) { return ((uintptr_t)p & (align - 1u)) == 0; }
static void *align_up(void *p, size_t align) {
    uintptr_t a = ((uintptr_t)p + (align - 1u)) & ~(uintptr_t)(align - 1u);
    return (unsigned char *)p + (a - (uintptr_t)p);   /* 원래 포인터에서 바이트 전진 */
}

/* 연습 5: 아무 주소에서나 32비트 읽기 — *(uint32_t*)p 대신 memcpy (정렬·aliasing 안전) */
static uint32_t load_u32(const void *p) { uint32_t v; memcpy(&v, p, sizeof v); return v; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static size_t list_dump(const struct node *h, int *out, size_t max) {
    size_t n = 0;
    for (; h && n < max; h = h->next) out[n++] = h->val;
    return n;
}

int main(void) {
    int out[8];

    /* 연습 1: 7 3 7 7 5 7 → remove_all(7) → 3 5 */
    static struct node a[6] = { {7, &a[1]}, {3, &a[2]}, {7, &a[3]}, {7, &a[4]}, {5, &a[5]}, {7, NULL} };
    struct node *h = &a[0];
    CHECK(list_remove_all(&h, 7) == 4);
    CHECK(h == &a[1] && list_dump(h, out, 8) == 2 && out[0] == 3 && out[1] == 5);
    CHECK(list_remove_all(&h, 42) == 0 && list_dump(h, out, 8) == 2);
    CHECK(list_remove_all(&h, 3) == 1 && list_remove_all(&h, 5) == 1 && h == NULL);

    /* 연습 2: 5, 1, 9, 3 순서로 넣어도 1 3 5 9 */
    static struct node b[4] = { {5, NULL}, {1, NULL}, {9, NULL}, {3, NULL} };
    struct node *s = NULL;
    for (int i = 0; i < 4; i++) list_insert_sorted(&s, &b[i]);
    CHECK(list_dump(s, out, 8) == 4 && out[0] == 1 && out[1] == 3 && out[2] == 5 && out[3] == 9);
    CHECK(s == &b[1]);                       /* head 가 1 로 바뀜 */

    /* 연습 3: 1 3 5 9 → 9 5 3 1, 빈 리스트도 OK */
    list_reverse(&s);
    CHECK(list_dump(s, out, 8) == 4 && out[0] == 9 && out[3] == 1 && s == &b[2]);
    struct node *empty = NULL;
    list_reverse(&empty);
    CHECK(empty == NULL);

    /* 연습 4 */
    static _Alignas(64) unsigned char pool[128];
    CHECK(align_up(pool, 32) == (void *)pool);              /* 이미 정렬됨 → 그대로 */
    CHECK(align_up(pool + 1, 32) == (void *)(pool + 32));
    CHECK(align_up(pool + 33, 4) == (void *)(pool + 36));
    CHECK(is_aligned(pool + 36, 4) && !is_aligned(pool + 37, 4));

    /* 연습 5: 비정렬 주소 bytes+1 에서 읽기 (리틀 엔디언 호스트 기준) */
    static const unsigned char bytes[5] = { 0x00, 0x11, 0x22, 0x33, 0x44 };
    CHECK(load_u32(bytes + 1) == 0x44332211u);
    CHECK(load_u32(bytes) == 0x33221100u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
