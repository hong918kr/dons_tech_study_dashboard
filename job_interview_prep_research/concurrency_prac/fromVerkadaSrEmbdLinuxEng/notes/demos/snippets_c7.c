/* C7 노트에 실린 스니펫들을 실제 컨텍스트에 넣고 컴파일한다 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define AP_TABLE_CAP 256u
#define WD_MAX_WORKERS 32
struct ApEntry { uint8_t bssid[6]; char ssid[33]; float rssi_smoothed; uint32_t times_seen; uint64_t last_seen_us; };
typedef struct { bool used; struct ApEntry e; } ap_slot_t;
static struct { pthread_mutex_t lock; ap_slot_t slot[AP_TABLE_CAP]; } g_tab = {.lock = PTHREAD_MUTEX_INITIALIZER};
static uint64_t wifi_now_us(void) { return 1000000ull; }
static bool entry_stale(const struct ApEntry *e, uint64_t now) { return now - e->last_seen_us > 300000000ull; }

/* --- 노트 §4 --- */
static bool weaker(const struct ApEntry *a, const struct ApEntry *b)
{
    if (a->rssi_smoothed != b->rssi_smoothed)
        return a->rssi_smoothed < b->rssi_smoothed;
    return memcmp(a->bssid, b->bssid, 6) > 0;
}
static void heap_swap(struct ApEntry *a, struct ApEntry *b) { struct ApEntry t = *a; *a = *b; *b = t; }

static void heap_up(struct ApEntry *h, size_t i)
{
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (!weaker(&h[i], &h[p]))
            return;
        heap_swap(&h[i], &h[p]);
        i = p;
    }
}

static void heap_down(struct ApEntry *h, size_t m, size_t i)
{
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, w = i;
        if (l < m && weaker(&h[l], &h[w])) w = l;
        if (r < m && weaker(&h[r], &h[w])) w = r;
        if (w == i)
            return;
        heap_swap(&h[i], &h[w]);
        i = w;
    }
}

size_t wifi_top_k(struct ApEntry *out, size_t k);
size_t wifi_top_k(struct ApEntry *out, size_t k)
{
    uint64_t now = wifi_now_us();
    size_t m = 0;
    pthread_mutex_lock(&g_tab.lock);
    for (uint32_t j = 0; j < AP_TABLE_CAP; j++) {
        const ap_slot_t *s = &g_tab.slot[j];
        if (!s->used || entry_stale(&s->e, now))
            continue;
        if (m < k) {
            out[m] = s->e;
            heap_up(out, m);
            m++;
        } else if (weaker(&out[0], &s->e)) {
            out[0] = s->e;
            heap_down(out, m, 0);
        }
    }
    pthread_mutex_unlock(&g_tab.lock);

    for (size_t i = 1; i < m; i++) {
        struct ApEntry v = out[i];
        size_t j = i;
        while (j > 0 && weaker(&out[j - 1], &v)) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = v;
    }
    return m;
}

/* --- 노트 §3, §5: 10번의 lazy 힙 --- */
typedef struct { uint64_t key; uint32_t id; } wd_node_t;
static wd_node_t g_heap[WD_MAX_WORKERS];
static int g_heap_n;
static int g_pos[WD_MAX_WORKERS];
static struct { bool in_use, faulted; uint64_t deadline_us; } g_cold[WD_MAX_WORKERS];
static _Atomic uint64_t g_last_seen[WD_MAX_WORKERS];

static void wd_heap_swap(int a, int b)
{
    wd_node_t t = g_heap[a];
    g_heap[a] = g_heap[b];
    g_heap[b] = t;
    g_pos[g_heap[a].id] = a;
    g_pos[g_heap[b].id] = b;
}
static void sift_down(int i)
{
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < g_heap_n && g_heap[l].key < g_heap[m].key) m = l;
        if (r < g_heap_n && g_heap[r].key < g_heap[m].key) m = r;
        if (m == i) return;
        wd_heap_swap(m, i);
        i = m;
    }
}
static uint64_t true_deadline(uint32_t id)
{
    return atomic_load_explicit(&g_last_seen[id], memory_order_acquire) + g_cold[id].deadline_us;
}

static bool heap_refresh_root(uint64_t now, uint64_t *out_key, uint32_t *out_id)
{
    while (g_heap_n > 0) {
        uint32_t id = g_heap[0].id;
        uint64_t truth = true_deadline(id);
        if (truth > now && g_cold[id].faulted) g_cold[id].faulted = false;
        if (truth <= g_heap[0].key) {
            *out_key = g_heap[0].key;
            *out_id  = id;
            return true;
        }
        g_heap[0].key = truth;
        sift_down(0);
    }
    return false;
}

/* --- 노트 §5: wd_overdue 의 가지치기 본문 --- */
size_t wd_overdue_frag(uint32_t *ids_out, size_t max, int i, uint64_t now, size_t n);
size_t wd_overdue_frag(uint32_t *ids_out, size_t max, int i, uint64_t now, size_t n)
{
    if (g_heap[i].key > now) return n;              /* prune subtree */
    uint32_t id = g_heap[i].id;
    if (true_deadline(id) <= now) {                 /* stale keys filtered */
        if (n < max) ids_out[n] = id;
        n++;
    }
    return n;
}

/* --- 노트 §5: v1 heartbeat --- */
void wd_heartbeat(uint32_t id);
void wd_heartbeat(uint32_t id)
{
    if (id >= WD_MAX_WORKERS) return;
    atomic_store_explicit(&g_last_seen[id], 12345ull, memory_order_release);
}

int main(void)
{
    struct ApEntry out[8];
    uint64_t k = 0; uint32_t id = 0; uint32_t ids[4];
    wd_heartbeat(0);
    (void)heap_refresh_root(1, &k, &id);
    (void)wd_overdue_frag(ids, 4, 0, 1, 0);
    return (int)wifi_top_k(out, 8);
}
