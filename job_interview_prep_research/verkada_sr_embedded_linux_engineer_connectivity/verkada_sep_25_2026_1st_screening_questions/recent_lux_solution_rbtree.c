/* recent_lux_solution_rbtree.c — Part 2 step 3 of the interviewer's progression:
 *   circular queue  ->  linked list  ->  RED-BLACK TREE
 *
 *   ./main.sh rbtree            normal run
 *   ./main.sh window rbtree     2-second window, exercises eviction
 *
 * Keyed by timestamp. Every operation the problem needs is O(log n):
 *   insert        new sample (also correct if it arrives out of order)
 *   floor(t)      "last sample with ts <= t"  -> get_lux_at
 *   delete-min    evict samples older than the window
 * and there is no capacity to guess up front (still capped for safety).
 * The Linux kernel ships the same structure (include/linux/rbtree.h) and uses
 * it for hrtimers, CFS run queues and epoll — ordered-by-time data.
 *
 * Classic CLRS red-black tree with a shared black sentinel (RB_NIL). All tree
 * operations run under g_hist.lock; malloc/free run outside it.
 */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "als.h"
#include "recent_lux.h"

#ifndef LUX_WINDOW_US
#define LUX_WINDOW_US (10ull * 60ull * 1000000ull)   /* 10 minutes */
#endif

#ifndef LUX_MAX_NODES
#define LUX_MAX_NODES 65536u                           /* 65536 * 48 B = 3 MiB worst case */
#endif

#define ERROR_BACKOFF_US 10000u

/* ------------------------------------------------------------------ */
/* red-black tree                                                      */
/* ------------------------------------------------------------------ */

enum { RB_RED, RB_BLACK };

typedef struct rb_node {
    uint64_t        ts;          /* key */
    float           lux;
    int             color;
    struct rb_node *left, *right, *parent;
} rb_node_t;

/* Sentinel: every leaf and the root's parent. Always black. Its parent field is
 * scratch space written by rb_delete (CLRS relies on that). */
static rb_node_t rb_nil_node = {.color = RB_BLACK};
#define RB_NIL (&rb_nil_node)

typedef struct {
    rb_node_t *root;
    uint32_t   count;
} rb_tree_t;

static void rb_rotate_left(rb_tree_t *t, rb_node_t *x)
{
    rb_node_t *y = x->right;
    x->right = y->left;
    if (y->left != RB_NIL)
        y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == RB_NIL)
        t->root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;
    y->left = x;
    x->parent = y;
}

static void rb_rotate_right(rb_tree_t *t, rb_node_t *x)
{
    rb_node_t *y = x->left;
    x->left = y->right;
    if (y->right != RB_NIL)
        y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == RB_NIL)
        t->root = y;
    else if (x == x->parent->right)
        x->parent->right = y;
    else
        x->parent->left = y;
    y->right = x;
    x->parent = y;
}

static void rb_insert_fixup(rb_tree_t *t, rb_node_t *z)
{
    while (z->parent->color == RB_RED) {
        rb_node_t *g = z->parent->parent;
        if (z->parent == g->left) {
            rb_node_t *uncle = g->right;
            if (uncle->color == RB_RED) {                 /* case 1: recolor, move up */
                z->parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                g->color = RB_RED;
                z = g;
            } else {
                if (z == z->parent->right) {              /* case 2: rotate into case 3 */
                    z = z->parent;
                    rb_rotate_left(t, z);
                }
                z->parent->color = RB_BLACK;              /* case 3 */
                z->parent->parent->color = RB_RED;
                rb_rotate_right(t, z->parent->parent);
            }
        } else {                                          /* mirror */
            rb_node_t *uncle = g->left;
            if (uncle->color == RB_RED) {
                z->parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                g->color = RB_RED;
                z = g;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rb_rotate_right(t, z);
                }
                z->parent->color = RB_BLACK;
                z->parent->parent->color = RB_RED;
                rb_rotate_left(t, z->parent->parent);
            }
        }
    }
    t->root->color = RB_BLACK;
}

/* Equal keys go right, so floor() returns the most recently inserted of equals. */
static void rb_insert(rb_tree_t *t, rb_node_t *z)
{
    rb_node_t *y = RB_NIL;
    rb_node_t *x = t->root;
    while (x != RB_NIL) {
        y = x;
        x = (z->ts < x->ts) ? x->left : x->right;
    }
    z->parent = y;
    if (y == RB_NIL)
        t->root = z;
    else if (z->ts < y->ts)
        y->left = z;
    else
        y->right = z;
    z->left = z->right = RB_NIL;
    z->color = RB_RED;
    rb_insert_fixup(t, z);
    t->count++;
}

static rb_node_t *rb_min(rb_node_t *x)
{
    while (x->left != RB_NIL)
        x = x->left;
    return x;
}

static rb_node_t *rb_successor(rb_node_t *x)
{
    if (x->right != RB_NIL)
        return rb_min(x->right);
    rb_node_t *y = x->parent;
    while (y != RB_NIL && x == y->right) {
        x = y;
        y = y->parent;
    }
    return y;
}

static void rb_transplant(rb_tree_t *t, rb_node_t *u, rb_node_t *v)
{
    if (u->parent == RB_NIL)
        t->root = v;
    else if (u == u->parent->left)
        u->parent->left = v;
    else
        u->parent->right = v;
    v->parent = u->parent;
}

static void rb_delete_fixup(rb_tree_t *t, rb_node_t *x)
{
    while (x != t->root && x->color == RB_BLACK) {
        if (x == x->parent->left) {
            rb_node_t *w = x->parent->right;
            if (w->color == RB_RED) {                                  /* case 1 */
                w->color = RB_BLACK;
                x->parent->color = RB_RED;
                rb_rotate_left(t, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == RB_BLACK && w->right->color == RB_BLACK) {
                w->color = RB_RED;                                     /* case 2 */
                x = x->parent;
            } else {
                if (w->right->color == RB_BLACK) {                     /* case 3 */
                    w->left->color = RB_BLACK;
                    w->color = RB_RED;
                    rb_rotate_right(t, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;                           /* case 4 */
                x->parent->color = RB_BLACK;
                w->right->color = RB_BLACK;
                rb_rotate_left(t, x->parent);
                x = t->root;
            }
        } else {                                                       /* mirror */
            rb_node_t *w = x->parent->left;
            if (w->color == RB_RED) {
                w->color = RB_BLACK;
                x->parent->color = RB_RED;
                rb_rotate_right(t, x->parent);
                w = x->parent->left;
            }
            if (w->right->color == RB_BLACK && w->left->color == RB_BLACK) {
                w->color = RB_RED;
                x = x->parent;
            } else {
                if (w->left->color == RB_BLACK) {
                    w->right->color = RB_BLACK;
                    w->color = RB_RED;
                    rb_rotate_left(t, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = RB_BLACK;
                w->left->color = RB_BLACK;
                rb_rotate_right(t, x->parent);
                x = t->root;
            }
        }
    }
    x->color = RB_BLACK;
}

/* Unlinks z (z itself leaves the tree, so the caller may free it). */
static void rb_delete(rb_tree_t *t, rb_node_t *z)
{
    rb_node_t *y = z, *x;
    int y_color = y->color;
    if (z->left == RB_NIL) {
        x = z->right;
        rb_transplant(t, z, z->right);
    } else if (z->right == RB_NIL) {
        x = z->left;
        rb_transplant(t, z, z->left);
    } else {
        y = rb_min(z->right);
        y_color = y->color;
        x = y->right;
        if (y->parent == z) {
            x->parent = y;
        } else {
            rb_transplant(t, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        rb_transplant(t, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }
    if (y_color == RB_BLACK)
        rb_delete_fixup(t, x);
    t->count--;
}

/* Last node with ts <= key, or RB_NIL. */
static rb_node_t *rb_floor(const rb_tree_t *t, uint64_t key)
{
    rb_node_t *x = t->root, *best = RB_NIL;
    while (x != RB_NIL) {
        if (x->ts <= key) {
            best = x;
            x = x->right;
        } else {
            x = x->left;
        }
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

static _Atomic uint32_t g_latest_bits = 0x7FC00000u;   /* Part 1, quiet NaN */

static struct {
    pthread_mutex_t lock;
    rb_tree_t       tree;
    uint32_t        overflow;
    uint32_t        alloc_fail;
} g_hist = {.lock = PTHREAD_MUTEX_INITIALIZER, .tree = {.root = RB_NIL}};

static _Atomic uint32_t g_errors;
static _Atomic bool     g_running;
static bool             g_started;
static pthread_t        g_thread;

/* ------------------------------------------------------------------ */
/* writer side                                                         */
/* ------------------------------------------------------------------ */

static void publish_latest(float lux)
{
    uint32_t bits;
    memcpy(&bits, &lux, sizeof bits);
    atomic_store_explicit(&g_latest_bits, bits, memory_order_release);
}

static void evict_min_locked(rb_node_t **garbage)
{
    rb_node_t *m = rb_min(g_hist.tree.root);
    rb_delete(&g_hist.tree, m);
    m->left = *garbage;                  /* reuse a pointer field as the free-list link */
    *garbage = m;
}

static void hist_append(uint64_t ts, float lux)
{
    rb_node_t *n = malloc(sizeof *n);    /* outside the lock */
    if (n == NULL) {
        g_hist.alloc_fail++;
        return;
    }
    n->ts = ts;                          /* no clamp: the tree accepts out-of-order keys */
    n->lux = lux;

    rb_node_t *garbage = NULL;
    uint64_t now = get_timestamp();

    pthread_mutex_lock(&g_hist.lock);
    /* keep-one-older: the minimum goes only when its successor is also out of the window */
    while (g_hist.tree.count >= 2) {
        rb_node_t *next = rb_successor(rb_min(g_hist.tree.root));
        if (next->ts > now || now - next->ts < LUX_WINDOW_US)
            break;
        evict_min_locked(&garbage);
    }
    if (g_hist.tree.count == LUX_MAX_NODES) {
        evict_min_locked(&garbage);
        g_hist.overflow++;
    }
    rb_insert(&g_hist.tree, n);
    pthread_mutex_unlock(&g_hist.lock);

    while (garbage) {
        rb_node_t *next = garbage->left;
        free(garbage);
        garbage = next;
    }
}

static void handle_reading(const SensorReading *r)
{
    switch (r->status) {
    case VALID:
        publish_latest(r->lux);
        hist_append(r->timestamp, r->lux);
        break;
    case NO_CHANGE:
        break;
    default:
        atomic_fetch_add_explicit(&g_errors, 1u, memory_order_relaxed);
        usleep(ERROR_BACKOFF_US);
        break;
    }
}

static void *sampler_main(void *arg)
{
    (void)arg;
    while (atomic_load_explicit(&g_running, memory_order_acquire)) {
        SensorReading r = read_next_sample();
        handle_reading(&r);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

int init_recent_lux(void)
{
    if (g_started)
        return 0;
    for (int tries = 0; tries < 3; tries++) {
        SensorReading r = read_next_sample();
        handle_reading(&r);
        if (r.status == VALID)
            break;
    }
    atomic_store_explicit(&g_running, true, memory_order_release);
    if (pthread_create(&g_thread, NULL, sampler_main, NULL) != 0) {
        atomic_store_explicit(&g_running, false, memory_order_release);
        return -1;
    }
    g_started = true;
    return 0;
}

static void rb_free_all(rb_node_t *x)
{
    while (x != RB_NIL) {                /* recurse left, loop right: depth <= 2 log n */
        rb_free_all(x->left);
        rb_node_t *right = x->right;
        free(x);
        x = right;
    }
}

void deinit_recent_lux(void)
{
    if (!g_started)
        return;
    atomic_store_explicit(&g_running, false, memory_order_release);
    pthread_join(g_thread, NULL);

    pthread_mutex_lock(&g_hist.lock);
    rb_node_t *root = g_hist.tree.root;
    g_hist.tree.root = RB_NIL;
    g_hist.tree.count = 0;
    pthread_mutex_unlock(&g_hist.lock);
    rb_free_all(root);
    g_started = false;
}

/* ------------------------------------------------------------------ */
/* Part 1                                                              */
/* ------------------------------------------------------------------ */

float get_most_recent_lux(void)
{
    uint32_t bits = atomic_load_explicit(&g_latest_bits, memory_order_acquire);
    float lux;
    memcpy(&lux, &bits, sizeof lux);
    return lux;
}

/* ------------------------------------------------------------------ */
/* Part 2: floor lookup                                                */
/* ------------------------------------------------------------------ */

float get_lux_at(uint64_t t)
{
    uint64_t now = get_timestamp();
    if (t > now || now - t > LUX_WINDOW_US)
        return NAN;

    float result = NAN;
    pthread_mutex_lock(&g_hist.lock);
    const rb_node_t *n = rb_floor(&g_hist.tree, t);
    if (n != RB_NIL)
        result = n->lux;
    pthread_mutex_unlock(&g_hist.lock);
    return result;
}
