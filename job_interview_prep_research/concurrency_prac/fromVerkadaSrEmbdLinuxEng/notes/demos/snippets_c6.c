/* C6 노트에 실린 스니펫들을 실제 컨텍스트에 넣고 컴파일한다 */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define AUDIT_DEDUPE_CAP 1024u
#define AUDIT_DEDUPE_WINDOW 16u
#define AUDIT_DEDUPE_RETENTION_US (30ull*1000000ull)
_Static_assert((AUDIT_DEDUPE_CAP & (AUDIT_DEDUPE_CAP - 1u)) == 0u, "dedupe cap must be 2^n");
#define HMASK (AUDIT_DEDUPE_CAP - 1u)

typedef struct { uint32_t id; uint64_t last_seen; bool used; } dedupe_slot_t;
static struct { pthread_mutex_t m; dedupe_slot_t map[AUDIT_DEDUPE_CAP];
                size_t live; uint64_t evictions, stale_reuse; } idx = {.m = PTHREAD_MUTEX_INITIALIZER};

/* --- 노트 §3 --- */
static uint32_t dedupe_home(uint32_t id)
{
    return (uint32_t)(id * 2654435761u) & HMASK;     /* Fibonacci hashing */
}

static uint32_t bssid_hash(const uint8_t b[6])
{
    uint32_t h = 2166136261u;             /* FNV offset basis */
    for (int i = 0; i < 6; i++) {
        h ^= (uint32_t)b[i];
        h *= 16777619u;                   /* FNV prime */
    }
    h ^= h >> 16;
    return h;
}

/* --- 노트 §4: 조회 --- */
int audit_is_duplicate(uint32_t badge_id, uint64_t now_us, uint64_t window_us);
int audit_is_duplicate(uint32_t badge_id, uint64_t now_us, uint64_t window_us)
{
    uint32_t home = dedupe_home(badge_id);
    int dup = 0;

    pthread_mutex_lock(&idx.m);
    for (uint32_t i = 0; i < AUDIT_DEDUPE_WINDOW; i++) {
        const dedupe_slot_t *e = &idx.map[(home + i) & HMASK];
        if (e->used && e->id == badge_id) {
            uint64_t t = e->last_seen;
            dup = (t <= now_us && now_us - t <= window_us) ? 1 : 0;
            break;
        }
    }
    pthread_mutex_unlock(&idx.m);
    return dup;
}

/* --- 노트 §4: 희생자 선택 --- */
void note_victim(uint32_t id, uint64_t ts, long first_stale, long first_empty, long oldest);
void note_victim(uint32_t id, uint64_t ts, long first_stale, long first_empty, long oldest)
{
    long victim;
    if (first_stale >= 0) {
        victim = first_stale;
        idx.stale_reuse++;
    } else if (first_empty >= 0) {
        victim = first_empty;
        idx.live++;
    } else {
        victim = oldest;
        idx.evictions++;
    }
    idx.map[victim].id = id;
    idx.map[victim].last_seen = ts;
    idx.map[victim].used = true;
}

/* --- 노트 §5: v0 (일부러 틀린 버전이지만 컴파일은 된다) --- */
static uint32_t ids[1024];
static uint64_t seen[1024];
static void note_v0(uint32_t id, uint64_t ts) { uint32_t s = id & 1023u; ids[s] = id; seen[s] = ts; }
static int  dup_v0(uint32_t id, uint64_t now, uint64_t win)
{
    uint32_t s = id & 1023u;
    return (ids[s] == id && now - seen[s] <= win) ? 1 : 0;
}

/* --- 노트 §7: brute force 참조 --- */
#define NIDS 600
static uint64_t ref_seen[NIDS];
static bool     ref_has[NIDS];
static int ref_is_dup(uint32_t id, uint64_t now, uint64_t win)
{
    if (!ref_has[id]) return 0;
    uint64_t t = ref_seen[id];
    return (t <= now && now - t <= win) ? 1 : 0;
}

int main(void)
{
    uint8_t mac[6] = {0,1,2,3,4,5};
    note_v0(7, 1); note_victim(9, 2, -1, 3, 4);
    return (int)(bssid_hash(mac) & 1u) + audit_is_duplicate(9, 5, 5) + dup_v0(7,1,1) + ref_is_dup(0,0,0);
}
