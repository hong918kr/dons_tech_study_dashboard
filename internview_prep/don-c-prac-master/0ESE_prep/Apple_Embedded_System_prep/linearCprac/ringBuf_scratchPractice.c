#include <stdio.h>
#include <stdbool.h>

#define CB_SIZE 5

typedef struct {
    int buf[CB_SIZE];
    int head;
    int tail;
    int count;
} cb_t;

void cb_init(cb_t* c) {
    c->head = 0;
    c->tail = 0;
    c->count = 0;
}

bool cb_push(cb_t* c, int val) {
    if (c->count == CB_SIZE) return false;
    c->buf[c->tail] = val;
    c->tail = (c->tail + 1) % CB_SIZE;
    c->count++;
    return true;
}

bool cb_pop(cb_t* c, int* val) {
    if (c->count == 0) return false;
    *val = c->buf[c->head];
    c->head = (c->head + 1) % CB_SIZE;
    c->count--;
    return true;
}

void cb_print(const cb_t* c) {
    printf("Buffer: ");
    int idx = c->head;
    for (int i = 0; i < c->count; ++i) {
        printf("%d ", c->buf[idx]);
        idx = (idx + 1) % CB_SIZE;
    }
    printf("(count=%d)\n", c->count);
}

void test_cb() {
    cb_t c;
    int v;
    bool ok;

    cb_init(&c);
    ok = cb_push(&c, 1) && cb_push(&c, 2) && cb_push(&c, 3);
    if (ok && c.count == 3) printf("Test 1 PASS\n"); else printf("Test 1 FAIL\n");

    ok = cb_pop(&c, &v);
    if (ok && v == 1 && c.count == 2) printf("Test 2 PASS\n"); else printf("Test 2 FAIL\n");

    ok = cb_push(&c, 4) && cb_push(&c, 5);
    ok = ok && cb_push(&c, 6); // 5번째 push도 성공해야 함
    bool full_fail = !cb_push(&c, 7); // 6번째 push에서 실패해야 함
    if (ok && full_fail && c.count == 5) printf("Test 3 PASS\n"); else printf("Test 3 FAIL\n");

    int expect[] = {2,3,4,5,6};
bool all_ok = true;
for (int i = 0; i < 5; ++i) {
    if (!cb_pop(&c, &v) || v != expect[i]) all_ok = false;
}
if (all_ok && c.count == 0) printf("Test 4 PASS\n"); else printf("Test 4 FAIL\n");

    ok = !cb_pop(&c, &v);
    if (ok && c.count == 0) printf("Test 5 PASS\n"); else printf("Test 5 FAIL\n");
}

int main() {
    test_cb();
    return 0;
}