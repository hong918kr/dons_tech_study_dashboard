/* DS-A05 demo_asm.c — 색 비트 인코딩이 실제로 어떤 명령이 되는가 / 코드 크기 (관찰용)
 * freestanding: 표준 라이브러리 함수를 부르지 않는다.
 *   cc -std=c11 -O2 -S demo_asm.c -o -
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S -o - demo_asm.c
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -c demo_asm.c -o d.o && llvm-size d.o
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RB_ROT() ((void)0)
#define RB_RECOLOR() ((void)0)
#define RB_API
#define container_of(ptr, type, member) ((type *)(void *)((char *)(ptr) - offsetof(type, member)))
#define RB_RED   0u
#define RB_BLACK 1u
#define RB_COLOR_MASK ((uintptr_t)1)
struct rb_node { uintptr_t parent_color; struct rb_node *rb_right, *rb_left; };
struct rb_root { struct rb_node *rb_node; };
_Static_assert(_Alignof(struct rb_node) >= 2, "bit 0 of a node address must be free for the colour");
#define RB_CLEAR_NODE(n) ((n)->parent_color = (uintptr_t)(n))
#define RB_EMPTY_NODE(n) ((n)->parent_color == (uintptr_t)(n))

static struct rb_node *rb_parent(const struct rb_node *n) { return (struct rb_node *)(n->parent_color & ~RB_COLOR_MASK); }
static unsigned rb_color(const struct rb_node *n) { return (unsigned)(n->parent_color & RB_COLOR_MASK); }
static bool rb_is_red(const struct rb_node *n) { return rb_color(n) == RB_RED; }
static bool rb_is_black(const struct rb_node *n) { return rb_color(n) == RB_BLACK; }
static void rb_set_parent(struct rb_node *n, struct rb_node *p) { n->parent_color = (uintptr_t)p | (n->parent_color & RB_COLOR_MASK); }
static void rb_set_black(struct rb_node *n) { if (rb_is_red(n)) RB_RECOLOR(); n->parent_color |= RB_COLOR_MASK; }
static void rb_set_red(struct rb_node *n) { if (rb_is_black(n)) RB_RECOLOR(); n->parent_color &= ~RB_COLOR_MASK; }
static bool rb_red_or_false(const struct rb_node *n) { return n && rb_is_red(n); }
static void rb_change_child(struct rb_node *old, struct rb_node *nw, struct rb_node *parent, struct rb_root *root) {
    if (parent) { if (parent->rb_left == old) parent->rb_left = nw; else parent->rb_right = nw; }
    else root->rb_node = nw;
}
__attribute__((unused)) static void rb_link_node(struct rb_node *node, struct rb_node *parent, struct rb_node **link) {
    node->parent_color = (uintptr_t)parent;
    node->rb_left = node->rb_right = NULL;
    *link = node;
}
static void rb_rotate_left(struct rb_node *x, struct rb_root *root) {
    struct rb_node *y = x->rb_right, *b = y->rb_left, *p = rb_parent(x);
    x->rb_right = b;
    if (b) rb_set_parent(b, x);
    y->rb_left = x;
    rb_set_parent(y, p);
    rb_set_parent(x, y);
    rb_change_child(x, y, p, root);
    RB_ROT();
}
static void rb_rotate_right(struct rb_node *x, struct rb_root *root) {
    struct rb_node *y = x->rb_left, *b = y->rb_right, *p = rb_parent(x);
    x->rb_left = b;
    if (b) rb_set_parent(b, x);
    y->rb_right = x;
    rb_set_parent(y, p);
    rb_set_parent(x, y);
    rb_change_child(x, y, p, root);
    RB_ROT();
}
RB_API void rb_insert_color(struct rb_node *node, struct rb_root *root) {
    struct rb_node *parent, *gparent, *uncle, *tmp;
    while ((parent = rb_parent(node)) && rb_is_red(parent)) {
        gparent = rb_parent(parent);
        if (parent == gparent->rb_left) {
            uncle = gparent->rb_right;
            if (rb_red_or_false(uncle)) { rb_set_black(parent); rb_set_black(uncle); rb_set_red(gparent); node = gparent; continue; }
            if (node == parent->rb_right) { rb_rotate_left(parent, root); tmp = node; node = parent; parent = tmp; }
            rb_set_black(parent); rb_set_red(gparent); rb_rotate_right(gparent, root);
            break;
        } else {
            uncle = gparent->rb_left;
            if (rb_red_or_false(uncle)) { rb_set_black(parent); rb_set_black(uncle); rb_set_red(gparent); node = gparent; continue; }
            if (node == parent->rb_left) { rb_rotate_right(parent, root); tmp = node; node = parent; parent = tmp; }
            rb_set_black(parent); rb_set_red(gparent); rb_rotate_left(gparent, root);
            break;
        }
    }
    rb_set_black(root->rb_node);
}
RB_API struct rb_node *rb_next(const struct rb_node *node) {
    struct rb_node *parent;
    if (RB_EMPTY_NODE(node)) return NULL;
    if (node->rb_right) { struct rb_node *n = node->rb_right; while (n->rb_left) n = n->rb_left; return n; }
    while ((parent = rb_parent(node)) && node == parent->rb_right) node = parent;
    return parent;
}
static void rb_erase_color(struct rb_node *parent, struct rb_root *root) {
    struct rb_node *node = NULL, *sib;
    for (;;) {
        sib = parent->rb_right;
        if (node != sib) {
            if (rb_is_red(sib)) { rb_set_black(sib); rb_set_red(parent); rb_rotate_left(parent, root); sib = parent->rb_right; }
            if (!rb_red_or_false(sib->rb_left) && !rb_red_or_false(sib->rb_right)) {
                rb_set_red(sib);
                if (rb_is_red(parent)) { rb_set_black(parent); return; }
                node = parent; parent = rb_parent(node);
                if (!parent) return;
                continue;
            }
            if (!rb_red_or_false(sib->rb_right)) { rb_set_black(sib->rb_left); rb_set_red(sib); rb_rotate_right(sib, root); sib = parent->rb_right; }
            if (rb_is_red(parent)) rb_set_red(sib); else rb_set_black(sib);
            rb_set_black(parent); rb_set_black(sib->rb_right); rb_rotate_left(parent, root);
            return;
        } else {
            sib = parent->rb_left;
            if (rb_is_red(sib)) { rb_set_black(sib); rb_set_red(parent); rb_rotate_right(parent, root); sib = parent->rb_left; }
            if (!rb_red_or_false(sib->rb_left) && !rb_red_or_false(sib->rb_right)) {
                rb_set_red(sib);
                if (rb_is_red(parent)) { rb_set_black(parent); return; }
                node = parent; parent = rb_parent(node);
                if (!parent) return;
                continue;
            }
            if (!rb_red_or_false(sib->rb_left)) { rb_set_black(sib->rb_right); rb_set_red(sib); rb_rotate_left(sib, root); sib = parent->rb_left; }
            if (rb_is_red(parent)) rb_set_red(sib); else rb_set_black(sib);
            rb_set_black(parent); rb_set_black(sib->rb_left); rb_rotate_right(parent, root);
            return;
        }
    }
}
RB_API void rb_erase(struct rb_node *node, struct rb_root *root) {
    struct rb_node *child, *fix_parent;
    bool removed_black;
    if (!node->rb_left || !node->rb_right) {
        struct rb_node *parent = rb_parent(node);
        child = node->rb_left ? node->rb_left : node->rb_right;
        removed_black = rb_is_black(node);
        rb_change_child(node, child, parent, root);
        if (child) rb_set_parent(child, parent);
        fix_parent = parent;
    } else {
        struct rb_node *succ = node->rb_right;
        while (succ->rb_left) succ = succ->rb_left;
        child = succ->rb_right;
        removed_black = rb_is_black(succ);
        if (succ == node->rb_right) {
            fix_parent = succ;
        } else {
            fix_parent = rb_parent(succ);
            fix_parent->rb_left = child;
            if (child) rb_set_parent(child, fix_parent);
            succ->rb_right = node->rb_right;
            rb_set_parent(succ->rb_right, succ);
        }
        succ->rb_left = node->rb_left;
        rb_set_parent(succ->rb_left, succ);
        rb_change_child(node, succ, rb_parent(node), root);
        succ->parent_color = node->parent_color;
    }
    if (removed_black) {
        if (child) rb_set_black(child);
        else if (fix_parent) rb_erase_color(fix_parent, root);
    }
}

/* 관찰할 작은 함수들 */
struct rb_node *demo_parent(const struct rb_node *n) { return rb_parent(n); }
_Bool demo_is_red(const struct rb_node *n) { return rb_is_red(n); }
void demo_set_parent(struct rb_node *n, struct rb_node *p) { rb_set_parent(n, p); }
