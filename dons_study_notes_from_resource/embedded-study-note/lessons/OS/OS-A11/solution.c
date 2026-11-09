/* OS-A11 종합: 멀티스레드 서비스 데몬
 *
 * kqueue 이벤트 루프 + 워커 스레드 풀로 도는 키-값(KV) 서버. 한 파일에 서버와 테스트가 같이 있다:
 *     ./so                 테스트 하네스 (자기 자신을 --server 로 띄워 시험한다)
 *     ./so --server ...    서버 (demo_load.c 도 이 모드를 띄운다)
 *
 *   Part A  슬랩 할당기, bounded queue(condvar), 완료 큐 → 이벤트 루프 깨우기
 *   Part B  SIGTERM graceful drain (접수한 요청은 하나도 잃지 않음),
 *           SIGHUP 원자적 설정 교체 (포인터 교체 + hazard slot 으로 안전한 회수)
 *   Part C  append-only 로그(WAL) + 재시작 복구 (찢어진 꼬리, CRC, SIGKILL)
 *
 * 스레드 소유권 규칙 (이 파일의 전부다):
 *   - conn_t, inflight, limbo 는 이벤트 루프 스레드만 만진다 → 락이 없다.
 *   - 워커는 req_t 만 만진다. 소유권은 bq(루프→워커)와 완료 큐(워커→루프)로 넘어간다.
 *   - kv 는 줄무늬(stripe) 락, slab 은 클래스별 락, wal 은 로그 락. 순서: stripe → wal, class → page.
 * 시그널은 EVFILT_SIGNAL 로 받는다 (핸들러 없음. self-pipe 판은 practice.c 연습 1).
 *
 * 체크 테스트는 매 실행 항상 성립하는 것만 본다 (응답 개수, 복구된 키). 시간 숫자는 보지 않는다.
 * "기다림"은 STATS 를 시한 안에서 폴링한다 → 결과는 결정적, 소요 시간만 변한다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define POISON(p, n)   ASAN_POISON_MEMORY_REGION((p), (n))
#define UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION((p), (n))
#endif
#endif
#ifndef POISON
#define POISON(p, n)   ((void)(p), (void)(n))   /* ASan 없을 땐 아무 일도 안 한다 */
#define UNPOISON(p, n) ((void)(p), (void)(n))
#endif

#define MAX_KEY     63
#define MAX_VAL     1024
#define IN_CAP      2048              /* 연결당 입력 버퍼 */
#define OUT_CAP     8192              /* 연결당 출력 버퍼 */
#define MAX_RESP    (MAX_VAL + 64)    /* 응답 한 줄의 상한: 이만큼 자리가 없으면 다음 요청을 파싱하지 않는다 */
#define MAX_WORKERS 16
#define LIMBO_CAP   32
#define MAX_EVENTS  64

static uint64_t now_ms(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000000u; }
static void sleep_ms(unsigned ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* ===================== Part A-1: 슬랩 할당기 ===================== */
#define SLAB_CLASSES 5
#define SLAB_PAGE    (128u * 1024u)
#define SLAB_HDR     64u              /* 페이지 머리(연결 리스트). 객체를 64 B 정렬로 맞춘다 */
static const size_t slab_sizes[SLAB_CLASSES] = { 64, 256, 1024, 4096, 16384 };

typedef struct slab_page { struct slab_page *next; } slab_page;
typedef struct {
    pthread_mutex_t m;
    void  *free_head;                 /* 반납된 객체의 침입형(intrusive) 연결 리스트. next 는 객체 첫 8 B */
    char  *bump;                      /* 아직 한 번도 안 나간 영역의 시작 */
    size_t bump_left;                 /* 그 영역에 남은 객체 수 */
    size_t used;                      /* 나가 있는 객체 수 */
} slab_class;
typedef struct {
    slab_class cls[SLAB_CLASSES];
    pthread_mutex_t pm;               /* 페이지 목록. 락 순서: cls[k].m → pm */
    slab_page *pages;
    size_t npages, max_pages;
} slab_t;

static void slab_init(slab_t *sl, size_t max_pages) {
    memset(sl, 0, sizeof *sl);
    for (int k = 0; k < SLAB_CLASSES; k++) pthread_mutex_init(&sl->cls[k].m, NULL);
    pthread_mutex_init(&sl->pm, NULL);
    sl->max_pages = max_pages;
}
static void slab_destroy(slab_t *sl) {
    for (slab_page *p = sl->pages; p; ) {
        slab_page *n = p->next;
        UNPOISON(p, SLAB_PAGE);
        free(p);
        p = n;
    }
    for (int k = 0; k < SLAB_CLASSES; k++) pthread_mutex_destroy(&sl->cls[k].m);
    pthread_mutex_destroy(&sl->pm);
}
/* n 바이트를 담는 가장 작은 클래스 번호. 너무 크면 -1 */
static int slab_class_of(size_t n) {
    for (int k = 0; k < SLAB_CLASSES; k++) if (n <= slab_sizes[k]) return k;
    return -1;
}
/* 새 페이지. 상한이면 NULL. 머리를 뺀 전체를 독(poison)으로 칠해서 돌려준다 */
static char *slab_new_page(slab_t *sl) {
    pthread_mutex_lock(&sl->pm);
    char *pg = NULL;
    if (sl->npages < sl->max_pages && posix_memalign((void **)&pg, SLAB_HDR, SLAB_PAGE) == 0) {
        ((slab_page *)pg)->next = sl->pages;
        sl->pages = (slab_page *)pg;
        sl->npages++;
        POISON(pg + SLAB_HDR, SLAB_PAGE - SLAB_HDR);
    }
    pthread_mutex_unlock(&sl->pm);
    return pg;
}
static size_t slab_used(slab_t *sl) {
    size_t u = 0;
    for (int k = 0; k < SLAB_CLASSES; k++) {
        pthread_mutex_lock(&sl->cls[k].m);
        u += sl->cls[k].used;
        pthread_mutex_unlock(&sl->cls[k].m);
    }
    return u;
}

/* TODO(Part A-1): 클래스 k 에서 객체 하나를 내준다. 없으면 NULL (n 이 너무 크거나 페이지 상한).
 *   free_head 가 있으면 거기서 꺼내고(꺼내기 전에 UNPOISON — next 가 객체 안에 있다),
 *   없으면 bump 에서 잘라 준다. bump 도 비었으면 slab_new_page 로 채운다.
 *   used 를 맞추고, 내주는 객체 전체를 UNPOISON 한다. 전부 c->m 안에서. */
static void *slab_alloc(slab_t *sl, size_t n) {
    int k = slab_class_of(n);
    if (k < 0) return NULL;
    slab_class *c = &sl->cls[k];
    size_t sz = slab_sizes[k];
    pthread_mutex_lock(&c->m);
    void *p = c->free_head;
    if (p) {
        UNPOISON(p, sz);
        c->free_head = *(void **)p;
    } else {
        if (c->bump_left == 0) {
            char *pg = slab_new_page(sl);
            if (!pg) { pthread_mutex_unlock(&c->m); return NULL; }
            c->bump = pg + SLAB_HDR;
            c->bump_left = (SLAB_PAGE - SLAB_HDR) / sz;
        }
        p = c->bump;
        c->bump += sz;
        c->bump_left--;
        UNPOISON(p, sz);
    }
    c->used++;
    pthread_mutex_unlock(&c->m);
    return p;
}

/* TODO(Part A-1): 객체를 클래스 free list 머리에 되돌린다. next 를 쓴 다음 객체 전체를 POISON. */
static void slab_free(slab_t *sl, void *p, size_t n) {
    if (!p) return;
    int k = slab_class_of(n);
    slab_class *c = &sl->cls[k];
    pthread_mutex_lock(&c->m);
    *(void **)p = c->free_head;
    c->free_head = p;
    c->used--;
    POISON(p, slab_sizes[k]);
    pthread_mutex_unlock(&c->m);
}

/* ===================== Part A-2: bounded queue ===================== */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  nonempty;
    void  **buf;
    size_t  cap, head, count;
    bool    closed;
} bq_t;

static void bq_init(bq_t *q, size_t cap) {
    memset(q, 0, sizeof *q);
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->nonempty, NULL);
    q->buf = calloc(cap, sizeof(void *));
    q->cap = cap;
}
static void bq_destroy(bq_t *q) {
    free(q->buf);
    pthread_mutex_destroy(&q->m);
    pthread_cond_destroy(&q->nonempty);
}
static size_t bq_depth(bq_t *q) {
    pthread_mutex_lock(&q->m);
    size_t n = q->count;
    pthread_mutex_unlock(&q->m);
    return n;
}

/* TODO(Part A-2): 이벤트 루프가 부른다 — 절대 막히면 안 된다.
 *   닫혔거나 가득 차면 false. 넣었으면 소비자 하나를 깨우고 true. */
static bool bq_try_push(bq_t *q, void *item) {
    pthread_mutex_lock(&q->m);
    if (q->closed || q->count == q->cap) {
        pthread_mutex_unlock(&q->m);
        return false;
    }
    q->buf[(q->head + q->count) % q->cap] = item;
    q->count++;
    pthread_cond_signal(&q->nonempty);
    pthread_mutex_unlock(&q->m);
    return true;
}

/* TODO(Part A-2): 워커가 부른다. 항목이 올 때까지 잠든다(while + cond_wait).
 *   닫혔어도 남은 항목은 끝까지 내준다. 닫혔고 비었을 때만 false. */
static bool bq_pop(bq_t *q, void **out) {
    pthread_mutex_lock(&q->m);
    while (q->count == 0 && !q->closed)
        pthread_cond_wait(&q->nonempty, &q->m);
    if (q->count == 0) {
        pthread_mutex_unlock(&q->m);
        return false;
    }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    pthread_mutex_unlock(&q->m);
    return true;
}

/* TODO(Part A-2): closed 를 세우고 잠든 워커를 전부 깨운다. */
static void bq_close(bq_t *q) {
    pthread_mutex_lock(&q->m);
    q->closed = true;
    pthread_cond_broadcast(&q->nonempty);
    pthread_mutex_unlock(&q->m);
}

/* ===================== 공통: CRC, WAL 레코드, KV 표, 설정 ===================== */
static uint32_t crc32_of(const void *data, size_t n) {
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

#define WAL_MAGIC 0x314C4157u          /* "WAL1" */
#define WAL_HDR   12u                  /* magic, payload 길이, payload CRC */
#define OP_SET 1
#define OP_DEL 2
#define OP_GET 3
#define OP_CFG 4
#define WAL_REC_MAX (WAL_HDR + 5u + MAX_KEY + MAX_VAL)

/* 레코드 = [magic u32][plen u32][crc u32] [op u8][klen u16][vlen u16][key][value]. 한 호스트 전용이라 네이티브 엔디언 */
static size_t wal_encode(uint8_t *buf, int op, const char *k, size_t klen, const char *v, size_t vlen) {
    uint8_t *pl = buf + WAL_HDR;
    uint16_t kl = (uint16_t)klen, vl = (uint16_t)vlen;
    pl[0] = (uint8_t)op;
    memcpy(pl + 1, &kl, 2);
    memcpy(pl + 3, &vl, 2);
    memcpy(pl + 5, k, klen);
    if (vlen) memcpy(pl + 5 + klen, v, vlen);
    uint32_t plen = (uint32_t)(5 + klen + vlen), magic = WAL_MAGIC, crc = crc32_of(pl, plen);
    memcpy(buf, &magic, 4);
    memcpy(buf + 4, &plen, 4);
    memcpy(buf + 8, &crc, 4);
    return WAL_HDR + plen;
}

typedef struct {
    int fd;
    pthread_mutex_t m;
} wal_t;

/* TODO(Part C): 레코드를 인코딩해 파일 끝에 한 번의 "완결된 쓰기"로 붙인다 (w->m 안에서).
 *   짧은 쓰기(short write)와 EINTR 은 이어서 쓴다.
 *   durability: 0 = write 만, 1 = fsync, 2 = fcntl(F_FULLFSYNC). 성공 0, 실패 -1. */
static int wal_append(wal_t *w, int op, const char *k, size_t klen, const char *v, size_t vlen, int durability) {
    uint8_t rec[WAL_REC_MAX];
    size_t n = wal_encode(rec, op, k, klen, v, vlen), off = 0;
    int rc = 0;
    pthread_mutex_lock(&w->m);
    while (off < n) {
        ssize_t got = write(w->fd, rec + off, n - off);
        if (got < 0) {
            if (errno == EINTR) continue;
            rc = -1;
            break;
        }
        off += (size_t)got;
    }
    if (rc == 0 && durability == 1 && fsync(w->fd) != 0) rc = -1;
    if (rc == 0 && durability == 2 && fcntl(w->fd, F_FULLFSYNC) != 0) rc = -1;
    pthread_mutex_unlock(&w->m);
    return rc;
}

#define BUCKETS 4096
#define STRIPES 64
typedef struct node {
    struct node *next;
    uint16_t klen, vlen;
    char data[];                       /* key 다음에 value */
} node_t;
typedef struct {
    node_t *b[BUCKETS];
    pthread_mutex_t lk[STRIPES];
    slab_t *sl;
} kv_t;

static void kv_init(kv_t *kv, slab_t *sl) {
    memset(kv, 0, sizeof *kv);
    kv->sl = sl;
    for (int i = 0; i < STRIPES; i++) pthread_mutex_init(&kv->lk[i], NULL);
}
static void kv_destroy(kv_t *kv) {
    for (int i = 0; i < STRIPES; i++) pthread_mutex_destroy(&kv->lk[i]);
}
static size_t kv_hash(const char *k, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)k[i]) * 16777619u;
    return h % BUCKETS;
}
static size_t node_size(size_t klen, size_t vlen) { return sizeof(node_t) + klen + vlen; }

/* node 를 먼저 잡고 → 로그 → 연결. 순서가 중요하다: 로그에 쓴 뒤 메모리가 없어 실패하면 복구 결과가 달라진다.
 * wal == NULL 이면 복구 경로(로그를 다시 쓰지 않는다). 반환 0, -1 메모리, -2 입출력 */
static int kv_set(kv_t *kv, wal_t *wal, int dur, const char *k, size_t klen, const char *v, size_t vlen) {
    size_t b = kv_hash(k, klen);
    node_t *n = slab_alloc(kv->sl, node_size(klen, vlen));
    if (!n) return -1;
    n->klen = (uint16_t)klen; n->vlen = (uint16_t)vlen;
    memcpy(n->data, k, klen);
    memcpy(n->data + klen, v, vlen);
    pthread_mutex_t *lk = &kv->lk[b % STRIPES];
    pthread_mutex_lock(lk);
    if (wal && wal_append(wal, OP_SET, k, klen, v, vlen, dur) != 0) {
        pthread_mutex_unlock(lk);
        slab_free(kv->sl, n, node_size(klen, vlen));
        return -2;
    }
    node_t **pp = &kv->b[b], *old = NULL;
    for (; *pp; pp = &(*pp)->next)
        if ((*pp)->klen == klen && memcmp((*pp)->data, k, klen) == 0) { old = *pp; *pp = old->next; break; }
    n->next = kv->b[b];
    kv->b[b] = n;
    pthread_mutex_unlock(lk);
    if (old) slab_free(kv->sl, old, node_size(old->klen, old->vlen));
    return 0;
}
static int kv_get(kv_t *kv, const char *k, size_t klen, char *out, size_t *vlen) {
    size_t b = kv_hash(k, klen);
    int found = 0;
    pthread_mutex_lock(&kv->lk[b % STRIPES]);
    for (node_t *n = kv->b[b]; n; n = n->next)
        if (n->klen == klen && memcmp(n->data, k, klen) == 0) {
            memcpy(out, n->data + klen, n->vlen);
            *vlen = n->vlen;
            found = 1;
            break;
        }
    pthread_mutex_unlock(&kv->lk[b % STRIPES]);
    return found;
}
/* 반환 1 지움, 0 없음, -2 입출력 */
static int kv_del(kv_t *kv, wal_t *wal, int dur, const char *k, size_t klen) {
    size_t b = kv_hash(k, klen);
    pthread_mutex_t *lk = &kv->lk[b % STRIPES];
    pthread_mutex_lock(lk);
    for (node_t **pp = &kv->b[b]; *pp; pp = &(*pp)->next) {
        node_t *n = *pp;
        if (n->klen == klen && memcmp(n->data, k, klen) == 0) {
            if (wal && wal_append(wal, OP_DEL, k, klen, "", 0, dur) != 0) {
                pthread_mutex_unlock(lk);
                return -2;
            }
            *pp = n->next;
            pthread_mutex_unlock(lk);
            slab_free(kv->sl, n, node_size(n->klen, n->vlen));
            return 1;
        }
    }
    pthread_mutex_unlock(lk);
    return 0;
}

typedef struct { uint64_t records; off_t valid_end, truncated; } replay_t;

/* TODO(Part C): 로그 전체를 읽어 앞에서부터 레코드를 적용한다 (kv_set/kv_del, wal = NULL).
 *   파일이 없으면 0 건, 정상. 레코드 하나라도 이상하면(머리가 덜 왔다 / magic 틀림 / plen 이 범위 밖 /
 *   몸통이 덜 왔다 / CRC 틀림) 거기서 멈추고 그 앞까지만 믿는다: ftruncate(valid_end).
 *   out->records(적용 수), valid_end, truncated(잘라낸 바이트) 를 채운다. 메모리 부족이면 -1, 아니면 0. */
static int wal_replay(const char *path, kv_t *kv, replay_t *out) {
    memset(out, 0, sizeof *out);
    int fd = open(path, O_RDWR);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return -1; }
    size_t size = (size_t)st.st_size, off = 0;
    uint8_t *m = malloc(size + 1);
    size_t got = 0;
    while (m && got < size) {
        ssize_t r = pread(fd, m + got, size - got, (off_t)got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    int rc = m ? 0 : -1;
    while (m && off + WAL_HDR <= got) {
        uint32_t magic, plen, crc;
        memcpy(&magic, m + off, 4);
        memcpy(&plen, m + off + 4, 4);
        memcpy(&crc, m + off + 8, 4);
        if (magic != WAL_MAGIC || plen < 5 || plen > 5u + MAX_KEY + MAX_VAL) break;
        if (off + WAL_HDR + plen > got) break;
        if (crc32_of(m + off + WAL_HDR, plen) != crc) break;
        uint8_t *pl = m + off + WAL_HDR;
        uint16_t kl, vl;
        memcpy(&kl, pl + 1, 2);
        memcpy(&vl, pl + 3, 2);
        if (5u + kl + vl != plen) break;
        if (pl[0] == OP_SET) {
            if (kv_set(kv, NULL, 0, (char *)pl + 5, kl, (char *)pl + 5 + kl, vl) != 0) { rc = -1; break; }
        } else if (pl[0] == OP_DEL) {
            (void)kv_del(kv, NULL, 0, (char *)pl + 5, kl);
        } else {
            break;
        }
        out->records++;
        off += WAL_HDR + plen;
    }
    out->valid_end = (off_t)off;
    out->truncated = (off_t)size - (off_t)off;
    if (rc == 0 && out->truncated > 0 && ftruncate(fd, out->valid_end) != 0) rc = -1;
    free(m);
    close(fd);
    return rc;
}

/* 설정 — 한 번 게시되면 불변(immutable). 교체는 새 객체 + 포인터 교환으로만 한다 */
typedef struct {
    int  version, max_value, durability, alpha, beta;   /* 불변식: beta == 2*alpha → 찢어짐 검출용 */
    char name[32];
} cfg_t;

static cfg_t *cfg_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    cfg_t *c = calloc(1, sizeof *c);
    char line[128];
    int seen = 0;
    while (c && fgets(line, sizeof line, f)) {
        char key[32], val[64];
        if (sscanf(line, "%31[^=]=%63s", key, val) != 2) { free(c); c = NULL; break; }
        if (!strcmp(key, "version"))         { c->version = atoi(val); seen |= 1; }
        else if (!strcmp(key, "max_value"))  { c->max_value = atoi(val); seen |= 2; }
        else if (!strcmp(key, "durability")) { c->durability = atoi(val); seen |= 4; }
        else if (!strcmp(key, "alpha"))      { c->alpha = atoi(val); seen |= 8; }
        else if (!strcmp(key, "beta"))       { c->beta = atoi(val); seen |= 16; }
        else if (!strcmp(key, "name"))       { snprintf(c->name, sizeof c->name, "%s", val); seen |= 32; }
        else { free(c); c = NULL; break; }
    }
    fclose(f);
    /* 검증을 통과해야만 게시된다: 전부 아니면 전무 */
    if (c && (seen != 63 || c->max_value <= 0 || c->max_value > MAX_VAL || c->durability < 0 ||
              c->durability > 2 || c->beta != 2 * c->alpha)) { free(c); c = NULL; }
    return c;
}
static void cfg_free(cfg_t *c) {
    if (c) memset(c, 0xDD, sizeof *c);          /* ASan 없이 돌려도 쓰레기가 눈에 띄게 */
    free(c);
}

/* ===================== 서버 상태 ===================== */
typedef struct conn {
    int fd;
    bool busy;                         /* 요청 1 개가 큐/워커에 있다. 연결당 동시 1 개 → 응답 순서가 자동 보장 */
    bool stalled;                      /* 큐가 가득 차 다음 요청을 못 넣고 기다린다 */
    bool eof, dead;                    /* 상대가 닫음 / 쓰기 실패 */
    bool rd_on, wr_on;
    size_t in_len, out_off, out_len;
    struct conn *prev, *next, *zombie_next;
    char in[IN_CAP];
    char out[OUT_CAP];
} conn_t;

typedef struct req {
    struct req *next;
    conn_t *c;
    int op;
    size_t klen, vlen;
    int resp_len;
    char key[MAX_KEY + 1];
    char val[MAX_VAL + 1];
    char resp[MAX_RESP];
} req_t;

typedef struct srv srv_t;
typedef struct { srv_t *s; int id; } worker_t;

enum { FAULT_NONE, FAULT_DROP_QUEUE, FAULT_FREE_NOW };

struct srv {
    int kq, lfd;
    const char *sock_path, *cfg_path;
    conn_t *conns, *zombies;
    int nconn, nstalled, inflight;
    uint64_t accepted, completed, rejected;
    slab_t sl;
    kv_t kv;
    wal_t wal;
    bq_t q;
    int nworkers;
    pthread_t th[MAX_WORKERS];
    worker_t w[MAX_WORKERS];
    pthread_mutex_t comp_m;            /* 완료 큐: 워커 → 이벤트 루프 */
    req_t *comp_head, *comp_tail;
    _Atomic(cfg_t *) cfg;              /* 현재 설정. 게시 = exchange */
    _Atomic(cfg_t *) hazard[MAX_WORKERS];
    cfg_t *limbo[LIMBO_CAP];           /* 교체됐지만 아직 못 지운 설정 (루프 전용) */
    int limbo_n, freed, reloads, reload_err;
    bool draining;
    uint64_t drain_deadline, drain_ms;
    pthread_mutex_t gate_m;            /* 테스트 훅: PAUSE/RESUME */
    pthread_cond_t gate_cv;
    bool gate_closed;
    atomic_int held;
    int fault;
};

/* ===================== Part A-3: 워커 ↔ 이벤트 루프 ===================== */
static void conn_kick(srv_t *s, conn_t *c);
static void retry_stalled(srv_t *s);

/* TODO(Part A-3): 워커가 끝낸 요청 r 을 완료 큐 꼬리에 넣고(comp_m 안에서) 이벤트 루프를 깨운다:
 *   struct kevent ke; EV_SET(&ke, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL); kevent(s->kq, &ke, 1, NULL, 0, NULL);
 *   큐가 비어 있었을 때만 깨워도 된다 (왜? 힌트 참고). */
static void comp_push(srv_t *s, req_t *r) {
    r->next = NULL;
    pthread_mutex_lock(&s->comp_m);
    bool was_empty = (s->comp_head == NULL);
    if (s->comp_tail) s->comp_tail->next = r; else s->comp_head = r;
    s->comp_tail = r;
    pthread_mutex_unlock(&s->comp_m);
    if (was_empty) {
        struct kevent ke;
        EV_SET(&ke, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
        (void)kevent(s->kq, &ke, 1, NULL, 0, NULL);
    }
}

/* TODO(Part A-3): 이벤트 루프 스레드에서 돈다. 완료 큐를 통째로 떼어 와서(락은 짧게) 각 요청마다:
 *   r->c 의 busy 를 내리고 s->inflight--, s->completed++, 연결이 죽지 않았으면 응답을 out 버퍼에 복사
 *   (자리는 파싱 전에 MAX_RESP 만큼 확인했다), req 반납(slab_free(&s->sl, r, sizeof *r)), conn_kick(s, c).
 *   전부 끝나면 retry_stalled(s) 로 큐를 기다리던 연결들을 다시 돌린다. */
static void on_completions(srv_t *s) {
    pthread_mutex_lock(&s->comp_m);
    req_t *r = s->comp_head;
    s->comp_head = s->comp_tail = NULL;
    pthread_mutex_unlock(&s->comp_m);
    while (r) {
        req_t *next = r->next;
        conn_t *c = r->c;
        c->busy = false;
        s->inflight--;
        s->completed++;
        if (!c->dead) {
            memcpy(c->out + c->out_len, r->resp, (size_t)r->resp_len);
            c->out_len += (size_t)r->resp_len;
        }
        slab_free(&s->sl, r, sizeof *r);
        conn_kick(s, c);
        r = next;
    }
    retry_stalled(s);
}

/* 테스트 훅: 워커가 설정을 쥔 채로 여기서 멈춘다 → 신호/리로드 순서를 강제할 수 있다. 시한 있음 */
static void gate_wait(srv_t *s) {
    uint64_t limit = now_ms() + 15000;
    pthread_mutex_lock(&s->gate_m);
    if (s->gate_closed) {
        atomic_fetch_add(&s->held, 1);
        while (s->gate_closed && now_ms() < limit) {
            struct timespec ts = { 0, 20 * 1000000L };
            pthread_cond_timedwait_relative_np(&s->gate_cv, &s->gate_m, &ts);
        }
        atomic_fetch_sub(&s->held, 1);
    }
    pthread_mutex_unlock(&s->gate_m);
}

/* ===================== Part B-2: 설정 읽기 · 게시 · 회수 ===================== */
/* TODO(Part B-2): 워커 id 의 hazard 슬롯에 현재 설정을 공개하고 돌려준다.
 *   읽고 → 슬롯에 쓰고 → 다시 읽어 같을 때까지 반복 (전부 seq_cst, 왜 그런지는 LLAT-M03/풀이 참고) */
static cfg_t *cfg_acquire(srv_t *s, int id) {
    cfg_t *c = atomic_load(&s->cfg);
    for (;;) {
        atomic_store(&s->hazard[id], c);
        cfg_t *again = atomic_load(&s->cfg);
        if (again == c) return c;
        c = again;
    }
}
/* TODO(Part B-2): 슬롯을 비운다 */
static void cfg_release(srv_t *s, int id) {
    atomic_store(&s->hazard[id], (cfg_t *)NULL);
}
/* TODO(Part B-2): 새 설정을 원자적으로 게시하고 옛 포인터를 돌려준다 */
static cfg_t *cfg_publish(srv_t *s, cfg_t *neu) {
    return atomic_exchange(&s->cfg, neu);
}
/* TODO(Part B-2): limbo 를 훑어서, 어떤 워커의 hazard 슬롯도 가리키지 않는 설정만 cfg_free 하고 s->freed++.
 *   (이 함수는 루프 스레드만 부른다. 순서 유지 필요 없음) */
static void limbo_scan(srv_t *s) {
    for (int i = 0; i < s->limbo_n; ) {
        bool pinned = false;
        for (int w = 0; w < s->nworkers && !pinned; w++)
            pinned = (atomic_load(&s->hazard[w]) == s->limbo[i]);
        if (pinned) { i++; continue; }
        cfg_free(s->limbo[i]);
        s->freed++;
        s->limbo[i] = s->limbo[--s->limbo_n];
    }
}

static void kv_exec(srv_t *s, req_t *r, cfg_t *cfg) {
    int n = 0, rc;
    char v[MAX_VAL];
    size_t vlen = 0;
    switch (r->op) {
    case OP_GET:
        if (kv_get(&s->kv, r->key, r->klen, v, &vlen))
            n = snprintf(r->resp, sizeof r->resp, "VAL %.*s\n", (int)vlen, v);
        else
            n = snprintf(r->resp, sizeof r->resp, "NF\n");
        break;
    case OP_SET:
        if (r->vlen > (size_t)cfg->max_value) { n = snprintf(r->resp, sizeof r->resp, "ERR toolarge\n"); break; }
        rc = kv_set(&s->kv, &s->wal, cfg->durability, r->key, r->klen, r->val, r->vlen);
        n = snprintf(r->resp, sizeof r->resp, rc == 0 ? "OK\n" : rc == -1 ? "ERR nomem\n" : "ERR io\n");
        break;
    case OP_DEL:
        rc = kv_del(&s->kv, &s->wal, cfg->durability, r->key, r->klen);
        n = snprintf(r->resp, sizeof r->resp, rc == 1 ? "OK\n" : rc == 0 ? "NF\n" : "ERR io\n");
        break;
    default:
        n = snprintf(r->resp, sizeof r->resp, "CFG %d %d %d %s\n", cfg->version, cfg->alpha, cfg->beta, cfg->name);
        break;
    }
    r->resp_len = n;
}

static void *worker_main(void *arg) {
    worker_t *w = arg;
    srv_t *s = w->s;
    void *item;
    while (bq_pop(&s->q, &item)) {
        req_t *r = item;
        cfg_t *c = cfg_acquire(s, w->id);   /* 요청 하나가 시작부터 끝까지 같은 버전을 본다 */
        gate_wait(s);
        kv_exec(s, r, c);
        cfg_release(s, w->id);
        comp_push(s, r);
    }
    return NULL;
}

/* ===================== 이벤트 루프 (제공) ===================== */
static void conn_interest(srv_t *s, conn_t *c) {
    bool want_rd = !c->eof && !c->dead && c->in_len < IN_CAP;
    bool want_wr = !c->dead && c->out_len > c->out_off;
    struct kevent ke[2];
    int n = 0;
    if (want_rd != c->rd_on) {
        EV_SET(&ke[n++], c->fd, EVFILT_READ, want_rd ? EV_ENABLE : EV_DISABLE, 0, 0, c);
        c->rd_on = want_rd;
    }
    if (want_wr != c->wr_on) {
        EV_SET(&ke[n++], c->fd, EVFILT_WRITE, want_wr ? EV_ENABLE : EV_DISABLE, 0, 0, c);
        c->wr_on = want_wr;
    }
    if (n) (void)kevent(s->kq, ke, n, NULL, 0, NULL);
}

static void conn_flush(conn_t *c) {
    while (!c->dead && c->out_off < c->out_len) {
        ssize_t n = write(c->fd, c->out + c->out_off, c->out_len - c->out_off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN) c->dead = true;
            break;
        }
        c->out_off += (size_t)n;
    }
    if (c->out_off == c->out_len) c->out_off = c->out_len = 0;
    else if (c->out_off > 0 && c->out_len + MAX_RESP > OUT_CAP) {   /* 뒤로 몰려 자리가 없으면 앞으로 당긴다 */
        memmove(c->out, c->out + c->out_off, c->out_len - c->out_off);
        c->out_len -= c->out_off;
        c->out_off = 0;
    }
}

static void conn_close(srv_t *s, conn_t *c) {
    if (c->stalled) { c->stalled = false; s->nstalled--; }
    close(c->fd);                      /* close 가 이 fd 의 kevent 등록을 지운다 */
    if (c->prev) c->prev->next = c->next; else s->conns = c->next;
    if (c->next) c->next->prev = c->prev;
    s->nconn--;
    c->zombie_next = s->zombies;       /* 이번 kevent 배치에 이 연결의 이벤트가 남아 있을 수 있다 → 배치 끝에 해제 */
    s->zombies = c;
    c->fd = -1;
}

static void out_str(conn_t *c, const char *msg) {
    size_t n = strlen(msg);
    memcpy(c->out + c->out_len, msg, n);
    c->out_len += n;
}

/* 줄 하나 처리. 1 = 소비, 0 = 큐가 가득 차 나중에 다시 (줄을 소비하지 않는다) */
static int handle_line(srv_t *s, conn_t *c, char *line) {
    char *save = NULL;
    char *cmd = strtok_r(line, " ", &save);
    if (!cmd) { out_str(c, "ERR empty\n"); return 1; }
    if (!strcmp(cmd, "STATS")) {
        char buf[320];
        cfg_t *cur = atomic_load(&s->cfg);
        snprintf(buf, sizeof buf,
                 "STATS q=%zu held=%d inflight=%d stalled=%d conns=%d drain=%d cfg=%d limbo=%d freed=%d "
                 "reloads=%d rerr=%d accepted=%llu completed=%llu rejected=%llu slab=%zu\n",
                 bq_depth(&s->q), atomic_load(&s->held), s->inflight, s->nstalled, s->nconn, s->draining,
                 cur->version, s->limbo_n, s->freed, s->reloads, s->reload_err,
                 (unsigned long long)s->accepted, (unsigned long long)s->completed,
                 (unsigned long long)s->rejected, slab_used(&s->sl));
        out_str(c, buf);
        return 1;
    }
    if (!strcmp(cmd, "PAUSE")) {
        pthread_mutex_lock(&s->gate_m);
        s->gate_closed = true;
        pthread_mutex_unlock(&s->gate_m);
        out_str(c, "OK\n");
        return 1;
    }
    if (!strcmp(cmd, "RESUME")) {
        pthread_mutex_lock(&s->gate_m);
        s->gate_closed = false;
        pthread_cond_broadcast(&s->gate_cv);
        pthread_mutex_unlock(&s->gate_m);
        out_str(c, "OK\n");
        return 1;
    }
    int op = !strcmp(cmd, "GET") ? OP_GET : !strcmp(cmd, "SET") ? OP_SET : !strcmp(cmd, "DEL") ? OP_DEL :
             !strcmp(cmd, "CFG") ? OP_CFG : 0;
    if (!op) { out_str(c, "ERR badcmd\n"); return 1; }
    if (s->draining) {                 /* 드레인 중 새 요청: 명시적으로 거절 (침묵 금지) */
        s->rejected++;
        out_str(c, "ERR draining\n");
        return 1;
    }
    char *k = strtok_r(NULL, " ", &save), *v = strtok_r(NULL, " ", &save);
    size_t klen = k ? strlen(k) : 0, vlen = v ? strlen(v) : 0;
    if ((op != OP_CFG && (klen == 0 || klen > MAX_KEY)) || vlen > MAX_VAL || (op == OP_SET && !v)) {
        out_str(c, "ERR badarg\n");
        return 1;
    }
    req_t *r = slab_alloc(&s->sl, sizeof *r);
    if (!r) { out_str(c, "ERR nomem\n"); return 1; }   /* 거절한다: 진행 중인 요청이 없으면 나중에 깨워 줄 완료도 없다 */
    r->c = c; r->op = op; r->klen = klen; r->vlen = vlen; r->resp_len = 0;
    if (k) memcpy(r->key, k, klen);
    if (v) memcpy(r->val, v, vlen);
    if (!bq_try_push(&s->q, r)) {
        slab_free(&s->sl, r, sizeof *r);
        return 0;
    }
    c->busy = true;
    s->inflight++;
    s->accepted++;
    return 1;
}

/* 버퍼에 쌓인 줄을 가능한 만큼 처리한다. 연결당 동시 요청 1 개, 출력 자리가 없으면 멈춘다 */
static void conn_pump(srv_t *s, conn_t *c) {
    while (!c->busy && !c->dead && c->out_len + MAX_RESP <= OUT_CAP) {
        char *nl = memchr(c->in, '\n', c->in_len);
        if (!nl) {
            if (c->in_len == IN_CAP) { out_str(c, "ERR toolong\n"); c->in_len = 0; c->eof = true; }
            break;
        }
        size_t len = (size_t)(nl - c->in);
        char line[IN_CAP];
        memcpy(line, c->in, len);
        line[len] = '\0';
        if (len && line[len - 1] == '\r') line[len - 1] = '\0';
        if (!handle_line(s, c, line)) {
            if (!c->stalled) { c->stalled = true; s->nstalled++; }
            break;
        }
        if (c->stalled) { c->stalled = false; s->nstalled--; }
        c->in_len -= len + 1;
        memmove(c->in, c->in + len + 1, c->in_len);
    }
}

static void conn_kick(srv_t *s, conn_t *c) {
    if (c->fd < 0) return;
    conn_pump(s, c);
    conn_flush(c);
    conn_pump(s, c);                   /* flush 로 출력 자리가 났을 수 있다 */
    conn_flush(c);
    bool line_left = memchr(c->in, '\n', c->in_len) != NULL;
    if (!c->busy && (c->dead || (c->eof && c->out_len == 0 && !line_left))) {
        conn_close(s, c);
        return;
    }
    conn_interest(s, c);
}

static void retry_stalled(srv_t *s) {
    if (s->nstalled == 0) return;
    for (conn_t *c = s->conns, *n; c; c = n) {
        n = c->next;                   /* conn_kick 이 c 를 닫을 수 있다 */
        if (c->stalled) conn_kick(s, c);
    }
}

static void on_listen(srv_t *s) {
    for (;;) {
        int fd = accept(s->lfd, NULL, NULL);
        if (fd < 0) return;
        conn_t *c = slab_alloc(&s->sl, sizeof *c);
        if (!c) { close(fd); continue; }
        memset(c, 0, offsetof(conn_t, in));
        c->fd = fd;
        (void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        int one = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        c->next = s->conns;
        if (s->conns) s->conns->prev = c;
        s->conns = c;
        s->nconn++;
        struct kevent ke[2];
        EV_SET(&ke[0], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, c);
        EV_SET(&ke[1], fd, EVFILT_WRITE, EV_ADD | EV_DISABLE, 0, 0, c);
        (void)kevent(s->kq, ke, 2, NULL, 0, NULL);
        c->rd_on = true;
    }
}

static void on_readable(srv_t *s, conn_t *c) {
    while (c->in_len < IN_CAP) {
        ssize_t n = read(c->fd, c->in + c->in_len, IN_CAP - c->in_len);
        if (n > 0) { c->in_len += (size_t)n; continue; }
        if (n == 0) c->eof = true;
        else if (errno == EINTR) continue;
        else if (errno != EAGAIN) c->dead = true;
        break;
    }
    conn_kick(s, c);
}

/* ===================== Part B-1: 드레인 ===================== */
/* TODO(Part B-1): SIGTERM 을 받았다. 여러 번 불려도 한 번만 동작해야 한다.
 *   draining 을 세우고 deadline 을 잡고(s->drain_deadline = now_ms() + s->drain_ms), 리슨 소켓을 닫고
 *   소켓 파일을 지운다(새 연결 거부). 이미 접수한 요청은 그대로 흐르게 둔다 — 여기서 큐를 비우면 안 된다. */
static void drain_begin(srv_t *s) {
    if (s->draining) return;
    s->draining = true;
    s->drain_deadline = now_ms() + s->drain_ms;
    close(s->lfd);
    s->lfd = -1;
    unlink(s->sock_path);
}
/* TODO(Part B-1): 드레인 완료 조건. 접수한 요청이 전부 끝났고(inflight == 0) 모든 연결의 응답이 나갔을 때 true */
static bool drain_finished(srv_t *s) {
    if (s->inflight != 0) return false;
    for (conn_t *c = s->conns; c; c = c->next)
        if (c->out_len > c->out_off && !c->dead) return false;
    return true;
}

static void on_signal(srv_t *s, int sig) {
    if (sig == SIGTERM) {
        drain_begin(s);
        if (s->fault == FAULT_DROP_QUEUE) {     /* 일부러 만든 버그: 종료할 때 큐를 버린다 */
            void *item;
            while (bq_depth(&s->q) > 0 && bq_pop(&s->q, &item))
                slab_free(&s->sl, item, sizeof(req_t));
        }
        conn_t *n;
        for (conn_t *c = s->conns; c; c = n) { n = c->next; conn_kick(s, c); }
        return;
    }
    cfg_t *neu = cfg_load(s->cfg_path);         /* SIGHUP */
    if (!neu || s->limbo_n == LIMBO_CAP) {
        s->reload_err++;
        free(neu);
        return;
    }
    cfg_t *old = cfg_publish(s, neu);
    if (!old) { free(neu); return; }            /* (starter: 아직 게시 안 함) */
    s->reloads++;
    if (s->fault == FAULT_FREE_NOW) cfg_free(old);   /* 일부러 만든 버그: 쥐고 있는 리더가 있어도 바로 해제 */
    else s->limbo[s->limbo_n++] = old;
}

static int srv_run(srv_t *s) {
    for (;;) {
        struct kevent ev[MAX_EVENTS];
        struct timespec ts = { 0, 20 * 1000000L };
        int n = kevent(s->kq, NULL, 0, ev, MAX_EVENTS, &ts);
        for (int i = 0; i < n; i++) {
            if (ev[i].flags & EV_ERROR) continue;
            if (ev[i].filter == EVFILT_SIGNAL)      on_signal(s, (int)ev[i].ident);
            else if (ev[i].filter == EVFILT_USER)   on_completions(s);
            else if ((int)ev[i].ident == s->lfd)    on_listen(s);
            else {
                conn_t *c = ev[i].udata;
                if (c->fd < 0) continue;        /* 같은 배치에서 이미 닫힌 연결 */
                if (ev[i].filter == EVFILT_READ)  on_readable(s, c);
                else                              conn_kick(s, c);
            }
        }
        while (s->zombies) {                    /* 배치가 끝났으니 이제 안전하게 해제 */
            conn_t *z = s->zombies;
            s->zombies = z->zombie_next;
            slab_free(&s->sl, z, sizeof *z);
        }
        limbo_scan(s);
        if (s->draining) {
            if (drain_finished(s)) return 0;
            if (now_ms() > s->drain_deadline) return 3;
        }
    }
}

static int server_main(int argc, char **argv) {
    /* --server SOCK LOG CFG WORKERS QCAP FAULT SLAB_PAGES DRAIN_MS */
    if (argc < 10) { fprintf(stderr, "usage\n"); return 2; }
    static srv_t srv;                  /* 스택 밖에 */
    srv_t *s = &srv;
    alarm(240);                        /* 고아 서버 방지 */
    s->sock_path = argv[2];
    s->cfg_path = argv[4];
    s->nworkers = atoi(argv[5]);
    if (s->nworkers < 1 || s->nworkers > MAX_WORKERS) return 2;
    s->fault = !strcmp(argv[7], "drop-queue") ? FAULT_DROP_QUEUE : !strcmp(argv[7], "free-now") ? FAULT_FREE_NOW : FAULT_NONE;
    s->drain_ms = (uint64_t)atoll(argv[9]);
    slab_init(&s->sl, (size_t)atol(argv[8]));
    kv_init(&s->kv, &s->sl);
    bq_init(&s->q, (size_t)atol(argv[6]));
    pthread_mutex_init(&s->comp_m, NULL);
    pthread_mutex_init(&s->gate_m, NULL);
    pthread_cond_init(&s->gate_cv, NULL);

    cfg_t *first = cfg_load(s->cfg_path);
    if (!first) { fprintf(stderr, "bad config\n"); return 2; }
    atomic_store(&s->cfg, first);

    replay_t rep;
    if (wal_replay(argv[3], &s->kv, &rep) != 0) { fprintf(stderr, "recovery failed\n"); return 4; }
    pthread_mutex_init(&s->wal.m, NULL);
    s->wal.fd = open(argv[3], O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (s->wal.fd < 0) return 2;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, SIG_IGN);          /* EVFILT_SIGNAL 은 SIG_IGN 이어도 기록된다 (man kqueue) */
    signal(SIGHUP, SIG_IGN);

    s->kq = kqueue();
    s->lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", s->sock_path);
    unlink(s->sock_path);
    if (bind(s->lfd, (struct sockaddr *)&a, sizeof a) != 0 || listen(s->lfd, 128) != 0) { perror("bind"); return 2; }
    (void)fcntl(s->lfd, F_SETFL, fcntl(s->lfd, F_GETFL) | O_NONBLOCK);
    struct kevent ke[4];
    EV_SET(&ke[0], s->lfd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    EV_SET(&ke[1], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
    EV_SET(&ke[2], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
    EV_SET(&ke[3], 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if (kevent(s->kq, ke, 4, NULL, 0, NULL) != 0) { perror("kevent"); return 2; }

    for (int i = 0; i < s->nworkers; i++) {
        s->w[i].s = s;
        s->w[i].id = i;
        pthread_create(&s->th[i], NULL, worker_main, &s->w[i]);
    }
    dprintf(1, "READY recovered=%llu truncated=%lld\n", (unsigned long long)rep.records, (long long)rep.truncated);

    int rc = srv_run(s);

    bq_close(&s->q);
    pthread_mutex_lock(&s->gate_m);
    s->gate_closed = false;
    pthread_cond_broadcast(&s->gate_cv);
    pthread_mutex_unlock(&s->gate_m);
    for (int i = 0; i < s->nworkers; i++) pthread_join(s->th[i], NULL);
    on_completions(s);                 /* 조인 뒤에 남은 완료를 비운다 (강제 종료 경로) */
    while (s->conns) conn_close(s, s->conns);
    while (s->zombies) { conn_t *z = s->zombies; s->zombies = z->zombie_next; slab_free(&s->sl, z, sizeof *z); }
    fsync(s->wal.fd);
    close(s->wal.fd);
    dprintf(1, "BYE accepted=%llu completed=%llu rejected=%llu lost=%llu slab_in_kv_or_leak=%zu\n",
            (unsigned long long)s->accepted, (unsigned long long)s->completed, (unsigned long long)s->rejected,
            (unsigned long long)(s->accepted - s->completed), slab_used(&s->sl));
    for (int i = 0; i < s->limbo_n; i++) cfg_free(s->limbo[i]);
    cfg_free(atomic_load(&s->cfg));
    kv_destroy(&s->kv);
    bq_destroy(&s->q);
    slab_destroy(&s->sl);
    return rc;
}

/* ===================== 테스트 하네스 ===================== */
static int g_total, g_pass;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
    else printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); fflush(stdout); } while (0)
static void skip(int n, const char *why) { g_total += n; printf("SKIP  %d checks (%s)\n", n, why); fflush(stdout); }

static char g_exe[PATH_MAX], g_dir[64], g_sock[128], g_log[128], g_cfg[128];
static volatile pid_t g_child;

static void on_watchdog(int sig) {
    (void)sig;
    static const char msg[] = "WATCHDOG: 시험이 시한을 넘겼다\n";
    if (write(2, msg, sizeof msg - 1) < 0) { /* 할 수 있는 게 없다 */ }
    if (g_child > 0) kill(g_child, SIGKILL);
    _exit(1);
}

typedef struct { pid_t pid; int out; long long rec, trunc; bool alive; char bye[256]; int status; } srvh;

static void write_cfg(int version, int alpha, int max_value) {
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s.tmp", g_cfg);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "version=%d\nmax_value=%d\ndurability=0\nalpha=%d\nbeta=%d\nname=gen%d\n", version, max_value, alpha, alpha * 2, version);
    fclose(f);
    rename(tmp, g_cfg);                /* 리더는 옛 파일 또는 새 파일만 본다 */
}

static bool read_child_line(int fd, char *buf, size_t cap, int timeout_ms) {
    size_t n = 0;
    uint64_t end = now_ms() + (uint64_t)timeout_ms;
    while (n + 1 < cap) {
        char ch;
        ssize_t r = read(fd, &ch, 1);
        if (r == 1) { if (ch == '\n') break; buf[n++] = ch; }
        else if (r == 0) return false;
        else if (errno == EAGAIN) { if (now_ms() > end) return false; sleep_ms(2); }
        else if (errno != EINTR) return false;
    }
    buf[n] = '\0';
    return true;
}

static bool srv_start(srvh *h, int workers, int qcap, const char *fault, int slab_pages, int drain_ms) {
    int p[2];
    memset(h, 0, sizeof *h);
    if (pipe(p) != 0) return false;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, p[1], 1);
    posix_spawn_file_actions_addclose(&fa, p[0]);
    posix_spawn_file_actions_addclose(&fa, p[1]);
    char w[16], q[16], sp[16], dm[16];
    snprintf(w, sizeof w, "%d", workers); snprintf(q, sizeof q, "%d", qcap);
    snprintf(sp, sizeof sp, "%d", slab_pages); snprintf(dm, sizeof dm, "%d", drain_ms);
    char *av[] = { g_exe, "--server", g_sock, g_log, g_cfg, w, q, (char *)fault, sp, dm, NULL };
    int e = posix_spawn(&h->pid, g_exe, &fa, NULL, av, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(p[1]);
    if (e != 0) { close(p[0]); return false; }
    g_child = h->pid;
    h->out = p[0];
    h->alive = true;
    (void)fcntl(p[0], F_SETFL, O_NONBLOCK);
    char line[128];
    if (!read_child_line(p[0], line, sizeof line, 20000)) return false;
    return sscanf(line, "READY recovered=%lld truncated=%lld", &h->rec, &h->trunc) == 2;
}
/* 프로세스가 끝나기를 시한까지 기다린다. 시한을 넘기면 SIGKILL. 정상 종료면 종료 코드, 아니면 -1 */
static int srv_wait(srvh *h, int timeout_ms) {
    uint64_t end = now_ms() + (uint64_t)timeout_ms;
    for (;;) {
        pid_t r = waitpid(h->pid, &h->status, WNOHANG);
        if (r == h->pid) break;
        if (now_ms() > end) {
            kill(h->pid, SIGKILL);
            waitpid(h->pid, &h->status, 0);
            h->alive = false; g_child = 0;
            printf("      (서버가 %d ms 안에 끝나지 않아 SIGKILL)\n", timeout_ms);
            (void)read_child_line(h->out, h->bye, sizeof h->bye, 10);
            close(h->out);
            return -1;
        }
        sleep_ms(2);
    }
    h->alive = false; g_child = 0;
    h->bye[0] = '\0';
    (void)read_child_line(h->out, h->bye, sizeof h->bye, 200);
    close(h->out);
    return WIFEXITED(h->status) ? WEXITSTATUS(h->status) : -1;
}
static void srv_sigkill(srvh *h) {
    kill(h->pid, SIGKILL);
    waitpid(h->pid, &h->status, 0);
    h->alive = false; g_child = 0;
    close(h->out);
}
static long long bye_field(const srvh *h, const char *key) {
    char pat[32];
    snprintf(pat, sizeof pat, "%s=", key);
    const char *p = strstr(h->bye, pat);
    return p ? atoll(p + strlen(pat)) : -1;
}

static int cl_connect(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", g_sock);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return -1; }
    struct timeval tv = { 8, 0 };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    return fd;
}
static bool cl_send(int fd, const char *msg) {
    size_t n = strlen(msg), off = 0;
    while (off < n) {
        ssize_t w = write(fd, msg + off, n - off);
        if (w <= 0) return false;
        off += (size_t)w;
    }
    return true;
}
/* 응답 한 줄. EOF 또는 시한 초과면 false */
static bool cl_line(int fd, char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        char ch;
        ssize_t r = read(fd, &ch, 1);
        if (r <= 0) return false;
        if (ch == '\n') { buf[n] = '\0'; return true; }
        buf[n++] = ch;
    }
    return false;
}
static bool cl_req(int fd, const char *msg, char *buf, size_t cap) { return cl_send(fd, msg) && cl_line(fd, buf, cap); }
static bool cl_is(int fd, const char *msg, const char *want) {
    char b[1200];
    return cl_req(fd, msg, b, sizeof b) && strcmp(b, want) == 0;
}
static long stat_of(int adm, const char *key) {
    char b[400], pat[32];
    if (!cl_req(adm, "STATS\n", b, sizeof b)) return -1;
    snprintf(pat, sizeof pat, " %s=", key);
    const char *p = strstr(b, pat);
    return p ? atol(p + strlen(pat)) : -1;
}
static bool wait_stat(int adm, const char *key, long want, int timeout_ms) {
    uint64_t end = now_ms() + (uint64_t)timeout_ms;
    long got = -1;
    while (now_ms() < end) {
        got = stat_of(adm, key);
        if (got == want) return true;
        sleep_ms(2);
    }
    printf("      (시한 초과: %s = %ld, 기대 %ld)\n", key, got, want);
    return false;
}
static void reset_state(void) { unlink(g_log); unlink(g_sock); write_cfg(1, 10, 900); }

/* ---- Part A: 단위 ---- */
typedef struct { slab_t *sl; int ok; } slab_arg;
static void *slab_stress(void *arg) {
    slab_arg *a = arg;
    void *held[64];
    size_t sz[64];
    uint32_t x = 2463534242u;
    a->ok = 1;
    for (int round = 0; round < 3000; round++) {
        int n = 0;
        for (int i = 0; i < 64; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            size_t s = 8 + (x % 4000);
            void *p = slab_alloc(a->sl, s);
            if (!p) continue;
            memset(p, (int)(x & 0xFF), s);
            held[n] = p; sz[n] = s; n++;
            ((unsigned char *)p)[0] = (unsigned char)i;
        }
        for (int i = 0; i < n; i++) {
            if (((unsigned char *)held[i])[0] != (unsigned char)i) a->ok = 0;
            slab_free(a->sl, held[i], sz[i]);
        }
    }
    return NULL;
}
static bool test_slab(void) {
    int before = g_total - g_pass;
    slab_t sl;
    slab_init(&sl, 4);
    void *a = slab_alloc(&sl, 10), *b = slab_alloc(&sl, 64), *c = slab_alloc(&sl, 65);
    CHECK(a != NULL && b != NULL && c != NULL);
    CHECK(a != b && labs((char *)b - (char *)a) >= 64);
    CHECK(slab_used(&sl) == 3);
    if (a && b && c) { memset(a, 1, 64); memset(b, 2, 64); memset(c, 3, 256); }
    CHECK(slab_alloc(&sl, 16385) == NULL);
    slab_free(&sl, b, 64);
    void *d = slab_alloc(&sl, 40);
    CHECK(d == b);                     /* 방금 반납한 객체가 같은 클래스에서 재사용된다 */
    if (a) slab_free(&sl, a, 10);
    if (c) slab_free(&sl, c, 65);
    if (d) slab_free(&sl, d, 40);
    CHECK(slab_used(&sl) == 0);
    slab_destroy(&sl);

    /* 페이지 상한: 16384 B 클래스는 페이지당 (128K-64)/16384 = 7 개, 페이지 2 장이면 14 개 */
    slab_init(&sl, 2);
    int got = 0;
    void *keep[20];
    for (int i = 0; i < 20; i++) { keep[i] = slab_alloc(&sl, 16000); if (keep[i]) got++; }
    CHECK(got == 14);
    for (int i = 0; i < 20; i++) if (keep[i]) slab_free(&sl, keep[i], 16000);
    void *again = slab_alloc(&sl, 16000);
    CHECK(again != NULL);              /* 다 돌려준 뒤 다시 나온다 */
    if (again) slab_free(&sl, again, 16000);
    slab_destroy(&sl);

    slab_init(&sl, 64);
    pthread_t t[4];
    slab_arg arg[4];
    for (int i = 0; i < 4; i++) { arg[i].sl = &sl; pthread_create(&t[i], NULL, slab_stress, &arg[i]); }
    int ok = 1;
    for (int i = 0; i < 4; i++) { pthread_join(t[i], NULL); ok &= arg[i].ok; }
    CHECK(ok == 1);
    CHECK(slab_used(&sl) == 0);
    slab_destroy(&sl);
    return (g_total - g_pass) == before;
}

typedef struct { bq_t *q; int id; long sum, cnt; atomic_int *started; } bq_arg;
static void *bq_consumer(void *arg) {
    bq_arg *a = arg;
    void *it;
    atomic_fetch_add(a->started, 1);
    while (bq_pop(a->q, &it)) { a->sum += (long)(intptr_t)it; a->cnt++; }
    return NULL;
}
static void *bq_consumer_one(void *arg) {
    bq_arg *a = arg;
    void *it;
    atomic_fetch_add(a->started, 1);
    if (bq_pop(a->q, &it)) a->sum = (long)(intptr_t)it;
    atomic_store(a->started, 100);
    return NULL;
}
static bool test_bq(void) {
    int before = g_total - g_pass;
    bq_t q;
    void *it = NULL;
    bq_init(&q, 3);
    CHECK(bq_try_push(&q, (void *)1) && bq_try_push(&q, (void *)2) && bq_try_push(&q, (void *)3));
    CHECK(!bq_try_push(&q, (void *)4));   /* 가득 참: 막히지 않고 false */
    CHECK(bq_depth(&q) == 3);
    CHECK(bq_pop(&q, &it) && it == (void *)1);
    CHECK(bq_try_push(&q, (void *)4));
    CHECK(bq_pop(&q, &it) && it == (void *)2);
    bq_close(&q);
    CHECK(!bq_try_push(&q, (void *)5));   /* 닫힌 뒤엔 안 받는다 */
    CHECK(bq_pop(&q, &it) && it == (void *)3);   /* 닫혀도 남은 건 나온다 */
    CHECK(bq_pop(&q, &it) && it == (void *)4);
    CHECK(!bq_pop(&q, &it));              /* 닫혔고 비었으면 false */
    bq_destroy(&q);

    /* 강제 스케줄: 소비자가 pop 에서 잠든 뒤에 넣는다 */
    atomic_int st = 0;
    bq_init(&q, 2);
    bq_arg one = { &q, 0, 0, 0, &st };
    pthread_t t;
    pthread_create(&t, NULL, bq_consumer_one, &one);
    for (int i = 0; i < 500 && atomic_load(&st) == 0; i++) sleep_ms(1);
    sleep_ms(30);
    CHECK(atomic_load(&st) == 1);          /* 아직 항목이 없으니 돌아오면 안 된다 */
    bool pushed = bq_try_push(&q, (void *)77);
    if (!pushed) bq_close(&q);             /* 틀린 구현이 영원히 자지 않게 */
    pthread_join(t, NULL);
    CHECK(pushed && one.sum == 77);
    bq_destroy(&q);

    /* 4 생산(한 스레드가 번갈아) · 4 소비: 정확히 한 번 */
    bq_init(&q, 8);
    atomic_int st2 = 0;
    bq_arg ca[4];
    pthread_t ct[4];
    for (int i = 0; i < 4; i++) { ca[i] = (bq_arg){ &q, i, 0, 0, &st2 }; pthread_create(&ct[i], NULL, bq_consumer, &ca[i]); }
    long want = 0;
    int spins = 0;
    for (long v = 1; v <= 20000 && spins < 200000; ) {      /* 진전이 없으면(스텁) 포기 */
        if (bq_try_push(&q, (void *)(intptr_t)v)) { want += v; v++; spins = 0; } else { spins++; sched_yield(); }
    }
    bq_close(&q);
    long sum = 0, cnt = 0;
    for (int i = 0; i < 4; i++) { pthread_join(ct[i], NULL); sum += ca[i].sum; cnt += ca[i].cnt; }
    CHECK(cnt == 20000 && sum == want);
    bq_destroy(&q);
    return (g_total - g_pass) == before;
}

/* ---- Part A: 서버 ---- */
typedef struct { int id; int rounds; int bad; } mc_arg;
static void *mc_client(void *arg) {
    mc_arg *a = arg;
    int fd = cl_connect();
    if (fd < 0) { a->bad = 1; return NULL; }
    for (int i = 0; i < a->rounds && !a->bad; i++) {
        char msg[80], want[64], got[64];
        snprintf(want, sizeof want, "v%d_%d", a->id, i);
        snprintf(msg, sizeof msg, "SET c%d %s\n", a->id, want);
        if (!cl_req(fd, msg, got, sizeof got) || strcmp(got, "OK")) a->bad = 1;
        snprintf(msg, sizeof msg, "GET c%d\n", a->id);
        char expect[80];
        snprintf(expect, sizeof expect, "VAL %s", want);
        if (!cl_req(fd, msg, got, sizeof got) || strcmp(got, expect)) a->bad = 1;
    }
    close(fd);
    return NULL;
}

static bool test_server_a(void) {
    int before = g_total - g_pass;
    srvh h;
    reset_state();
    bool up = srv_start(&h, 4, 16, "none", 64, 3000);
    CHECK(up);
    if (!up) { if (h.alive) srv_sigkill(&h); return false; }
    int fd = cl_connect();
    CHECK(fd >= 0);
    CHECK(cl_is(fd, "SET hello world\n", "OK"));
    bool got1 = cl_is(fd, "GET hello\n", "VAL world");
    CHECK(got1);
    bool srv_ok = got1;
    if (srv_ok) {
        CHECK(cl_is(fd, "GET nothing\n", "NF"));
        CHECK(cl_is(fd, "DEL hello\n", "OK"));
        CHECK(cl_is(fd, "GET hello\n", "NF"));
        CHECK(cl_is(fd, "NOPE\n", "ERR badcmd"));
        CHECK(cl_is(fd, "CFG\n", "CFG 1 10 20 gen1"));

        /* 조각난 요청 + 한 번에 온 여러 요청: 응답은 요청 순서대로 */
        const char *part = "SET sp";
        for (const char *p = part; *p; p++) { char c[2] = { *p, 0 }; (void)cl_send(fd, c); sleep_ms(1); }
        (void)cl_send(fd, "lit 123\nGET split\nGET nothing\nSET z 9\n");
        char l1[64] = "", l2[64] = "", l3[64] = "", l4[64] = "";
        bool ok4 = cl_line(fd, l1, sizeof l1) && cl_line(fd, l2, sizeof l2) && cl_line(fd, l3, sizeof l3) && cl_line(fd, l4, sizeof l4);
        CHECK(ok4 && !strcmp(l1, "OK") && !strcmp(l2, "VAL 123") && !strcmp(l3, "NF") && !strcmp(l4, "OK"));

        /* 큰 출력: 클라이언트가 읽기 전에 소켓 버퍼를 넘긴다 → EVFILT_WRITE 경로 */
        char big[901];
        memset(big, 'x', 900); big[900] = '\0';
        char msg[1100];
        snprintf(msg, sizeof msg, "SET big %s\n", big);
        (void)cl_req(fd, msg, l1, sizeof l1);
        char pipeline[300 * 8 + 1];
        pipeline[0] = '\0';
        size_t pl = 0;
        for (int i = 0; i < 300; i++) { memcpy(pipeline + pl, "GET big\n", 8); pl += 8; }
        pipeline[pl] = '\0';
        bool sent = cl_send(fd, pipeline);
        sleep_ms(50);
        int okv = 0;
        char lb[1200];
        for (int i = 0; i < 300; i++) {
            if (!cl_line(fd, lb, sizeof lb)) break;
            if (!strncmp(lb, "VAL x", 5) && strlen(lb) == 904) okv++;
        }
        CHECK(sent && okv == 300);

        /* 너무 긴 줄: 끝없는 입력으로 버퍼가 차면 거절하고 닫는다 */
        int lf = cl_connect();
        char longline[IN_CAP + 200];
        memset(longline, 'a', sizeof longline - 1);
        longline[sizeof longline - 1] = '\0';
        (void)cl_send(lf, longline);
        char tb[64] = "";
        CHECK(cl_line(lf, tb, sizeof tb) && !strcmp(tb, "ERR toolong"));
        close(lf);

        /* 다중 클라이언트 */
        mc_arg ma[8];
        pthread_t t[8];
        for (int i = 0; i < 8; i++) { ma[i] = (mc_arg){ i, 150, 0 }; pthread_create(&t[i], NULL, mc_client, &ma[i]); }
        int bad = 0;
        for (int i = 0; i < 8; i++) { pthread_join(t[i], NULL); bad += ma[i].bad; }
        CHECK(bad == 0);
    } else {
        skip(9, "기본 응답 실패");
    }
    close(fd);
    kill(h.pid, SIGKILL);              /* 드레인은 Part B 에서 본다 */
    waitpid(h.pid, &h.status, 0);
    h.alive = false; g_child = 0;
    close(h.out);
    if (!srv_ok) return false;

    /* 역압: 큐 4, 워커 1, 게이트 닫힘 → 1 개는 워커가 쥐고, 4 개는 큐에, 6 번째는 stalled */
    reset_state();
    up = srv_start(&h, 1, 4, "none", 64, 3000);
    CHECK(up);
    if (!up) { if (h.alive) srv_sigkill(&h); return false; }
    int adm = cl_connect();
    CHECK(cl_is(adm, "PAUSE\n", "OK"));
    int cs[6];
    for (int i = 0; i < 6; i++) {
        char msg[32];
        cs[i] = cl_connect();
        snprintf(msg, sizeof msg, "SET bp%d v\n", i);
        (void)cl_send(cs[i], msg);
        if (i == 0) (void)wait_stat(adm, "held", 1, 5000);   /* 첫 요청이 워커 손에 잡힌 뒤에 나머지를 보낸다 */
    }
    CHECK(wait_stat(adm, "stalled", 1, 5000));
    CHECK(stat_of(adm, "q") == 4 && stat_of(adm, "held") == 1);   /* 큐는 상한을 절대 넘지 않는다 */
    CHECK(cl_is(adm, "RESUME\n", "OK"));
    int okc = 0;
    for (int i = 0; i < 6; i++) { char b[32]; if (cl_line(cs[i], b, sizeof b) && !strcmp(b, "OK")) okc++; close(cs[i]); }
    CHECK(okc == 6);
    close(adm);
    srv_sigkill(&h);

    /* 슬랩 소진: 값 900 B → 1024 B 클래스. 페이지 8 장이면 언젠가 ERR nomem, DEL 뒤엔 다시 된다 */
    reset_state();
    up = srv_start(&h, 2, 16, "none", 8, 3000);
    CHECK(up);
    if (!up) { if (h.alive) srv_sigkill(&h); return false; }
    fd = cl_connect();
    char val[901], msg[1000], line[64];
    memset(val, 'y', 900); val[900] = '\0';
    int stored = 0;
    bool nomem = false;
    for (int i = 0; i < 2000 && !nomem; i++) {
        snprintf(msg, sizeof msg, "SET k%d %s\n", i, val);
        if (!cl_req(fd, msg, line, sizeof line)) break;
        if (!strcmp(line, "OK")) stored++;
        else if (!strcmp(line, "ERR nomem")) nomem = true;
    }
    CHECK(nomem && stored > 0);
    int freed = 0;
    for (int i = 0; i < stored; i++) {
        snprintf(msg, sizeof msg, "DEL k%d\n", i);
        if (cl_req(fd, msg, line, sizeof line) && !strcmp(line, "OK")) freed++;
    }
    snprintf(msg, sizeof msg, "SET again %s\n", val);
    CHECK(freed == stored && cl_is(fd, msg, "OK"));
    close(fd);
    srv_sigkill(&h);
    return (g_total - g_pass) == before;
}

/* ---- Part B ---- */
/* N 개 연결이 요청 1 개씩 넣은 상태에서 SIGTERM. 드레인 동안 새 연결·새 요청이 어떻게 되는지 본다 */
static void drain_scenario(const char *fault, bool expect_ok) {
    srvh h;
    reset_state();
    bool up = srv_start(&h, 1, 8, fault, 64, expect_ok ? 5000 : 400);
    CHECK(up);
    if (!up) { if (h.alive) srv_sigkill(&h); return; }
    int adm = cl_connect();
    (void)cl_is(adm, "PAUSE\n", "OK");
    enum { N = 6 };
    int cs[N];
    for (int i = 0; i < N; i++) {
        char msg[32];
        cs[i] = cl_connect();
        snprintf(msg, sizeof msg, "SET d%d v%d\n", i, i);
        (void)cl_send(cs[i], msg);
    }
    bool settled = wait_stat(adm, "held", 1, 5000) && wait_stat(adm, "q", N - 1, 5000);
    CHECK(settled);                    /* 1 개는 워커 손, 5 개는 큐 */
    kill(h.pid, SIGTERM);
    CHECK(wait_stat(adm, "drain", 1, 5000));
    if (expect_ok) {
        CHECK(cl_connect() == -1);     /* 리슨 소켓은 닫혔다 */
        CHECK(cl_is(adm, "GET d0\n", "ERR draining"));   /* 드레인 중 새 요청은 명시적 거절 */
    }
    (void)cl_is(adm, "RESUME\n", "OK");
    int ok = 0, eof = 0;
    for (int i = 0; i < N; i++) {
        char b[32];
        if (cl_line(cs[i], b, sizeof b) && !strcmp(b, "OK")) ok++;
        if (!cl_line(cs[i], b, sizeof b)) eof++;
        close(cs[i]);
    }
    close(adm);
    int rc = srv_wait(&h, 8000);
    if (expect_ok) {
        CHECK(rc == 0);
        CHECK(ok == N);                /* 접수한 요청은 전부 응답을 받았다 */
        CHECK(eof == N);               /* 그 다음 서버가 닫았다 */
        CHECK(bye_field(&h, "accepted") == N && bye_field(&h, "completed") == N && bye_field(&h, "lost") == 0);
        srvh h2;
        up = srv_start(&h2, 2, 8, "none", 64, 3000);
        CHECK(up && h2.rec == N);      /* 전부 로그에 있다 */
        if (up) {
            int fd = cl_connect();
            int found = 0;
            for (int i = 0; i < N; i++) {
                char msg[32], want[32];
                snprintf(msg, sizeof msg, "GET d%d\n", i);
                snprintf(want, sizeof want, "VAL v%d", i);
                found += cl_is(fd, msg, want);
            }
            CHECK(found == N);
            close(fd);
            kill(h2.pid, SIGTERM);
            CHECK(srv_wait(&h2, 8000) == 0);
        } else {
            if (h2.alive) srv_sigkill(&h2);
            skip(2, "재시작 실패");
        }
    } else {
        /* 일부러 큐를 버린 서버 → 검사 자체가 그것을 잡아내는지 본다 */
        printf("      fault=%s: 응답 %d/%d, 서버 종료 코드 %d, %s\n", fault, ok, N, rc, h.bye);
        CHECK(ok == 1);                /* 워커 손에 있던 1 개만 답을 받는다 */
        CHECK(rc == 3);                /* 드레인 시한 초과 종료 */
        CHECK(bye_field(&h, "lost") == N - 1);
    }
}

typedef struct { int id; atomic_long *acks; long last_ok; int rejected, unanswered, sent; } dl_arg;
static void *dl_client(void *arg) {
    dl_arg *a = arg;
    int fd = cl_connect();
    a->last_ok = -1;
    if (fd < 0) { a->unanswered = 1; return NULL; }
    for (long i = 0; i < 100000; i++) {
        char msg[64], b[64];
        snprintf(msg, sizeof msg, "SET t%d_%ld v\n", a->id, i);
        a->sent++;
        if (!cl_send(fd, msg) || !cl_line(fd, b, sizeof b)) { a->unanswered++; break; }
        if (!strcmp(b, "OK")) { a->last_ok = i; atomic_fetch_add(a->acks, 1); }
        else if (!strcmp(b, "ERR draining")) { a->rejected++; break; }
        else { a->unanswered++; break; }
    }
    close(fd);
    return NULL;
}
static bool wait_acks(atomic_long *acks, long want, int timeout_ms) {
    uint64_t end = now_ms() + (uint64_t)timeout_ms;
    while (now_ms() < end && atomic_load(acks) < want) sleep_ms(1);
    return atomic_load(acks) >= want;
}
/* 띄운 서버에 클라이언트 4 개를 붙여 ack 가 want 만큼 쌓이면 sig 를 보낸다 */
static void run_load_until(srvh *h, dl_arg *arg, long want, int sig) {
    atomic_long acks = 0;
    pthread_t t[4];
    for (int i = 0; i < 4; i++) { arg[i] = (dl_arg){ i, &acks, -1, 0, 0, 0 }; pthread_create(&t[i], NULL, dl_client, &arg[i]); }
    (void)wait_acks(&acks, want, 15000);
    kill(h->pid, sig);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
}
/* 재시작한 서버에서 ack 된 키가 전부 있는지 센다 */
static int count_missing(const dl_arg *arg) {
    int fd = cl_connect(), missing = 0;
    for (int i = 0; i < 4; i++)
        for (long k = 0; k <= arg[i].last_ok; k++) {
            char msg[64], want[8] = "VAL v";
            snprintf(msg, sizeof msg, "GET t%d_%ld\n", i, k);
            if (!cl_is(fd, msg, want)) missing++;
        }
    close(fd);
    return missing;
}

static void test_drain_under_load(void) {
    srvh h;
    dl_arg arg[4];
    reset_state();
    bool up = srv_start(&h, 4, 16, "none", 256, 5000);
    CHECK(up);
    if (!up) { if (h.alive) srv_sigkill(&h); return; }
    run_load_until(&h, arg, 400, SIGTERM);
    int rc = srv_wait(&h, 8000);
    CHECK(rc == 0);
    long ok = 0; int rej = 0, sent = 0, una = 0, una_max = 0;
    for (int i = 0; i < 4; i++) {
        ok += arg[i].last_ok + 1; rej += arg[i].rejected; sent += arg[i].sent; una += arg[i].unanswered;
        if (arg[i].unanswered > una_max) una_max = arg[i].unanswered;
    }
    printf("      부하 중 SIGTERM: sent=%d ok=%ld rejected=%d unanswered=%d  (%s)\n", sent, ok, rej, una, h.bye);
    CHECK(sent == ok + rej + una);     /* 모든 요청의 운명이 하나로 정해졌다 */
    CHECK(una_max <= 1);               /* 클라이언트당 최대 1 개만 "닫힌 뒤 보낸 것" */
    CHECK(bye_field(&h, "completed") == ok && bye_field(&h, "lost") == 0);
    srvh h2;
    up = srv_start(&h2, 2, 16, "none", 256, 3000);
    CHECK(up);
    if (up) {
        CHECK(count_missing(arg) == 0);
        kill(h2.pid, SIGTERM);
        (void)srv_wait(&h2, 8000);
    } else {
        if (h2.alive) srv_sigkill(&h2);
        skip(1, "재시작 실패");
    }
}

static void test_reload(void) {
    srvh h;
    reset_state();
    bool up = srv_start(&h, 2, 16, "none", 64, 3000);
    CHECK(up);
    if (!up) { if (h.alive) srv_sigkill(&h); return; }
    int adm = cl_connect(), fd = cl_connect();
    CHECK(cl_is(fd, "CFG\n", "CFG 1 10 20 gen1"));
    write_cfg(2, 21, 900);
    kill(h.pid, SIGHUP);
    CHECK(wait_stat(adm, "cfg", 2, 5000));
    CHECK(cl_is(fd, "CFG\n", "CFG 2 21 42 gen2"));

    /* 망가진 설정: 거부하고 옛 버전 유지 */
    FILE *f = fopen(g_cfg, "w");
    if (f) { fputs("version=3\nmax_value=-5\ngarbage\n", f); fclose(f); }
    kill(h.pid, SIGHUP);
    CHECK(wait_stat(adm, "rerr", 1, 5000));
    CHECK(cl_is(fd, "CFG\n", "CFG 2 21 42 gen2"));

    /* 강제 스케줄: 워커가 v2 를 쥔 채 게이트에서 멈춘 동안 v3 게시 → v2 는 해제되면 안 된다 */
    write_cfg(3, 33, 900);
    int freed0 = (int)stat_of(adm, "freed");
    (void)cl_is(adm, "PAUSE\n", "OK");
    (void)cl_send(fd, "CFG\n");
    CHECK(wait_stat(adm, "held", 1, 5000));
    kill(h.pid, SIGHUP);
    CHECK(wait_stat(adm, "cfg", 3, 5000));
    sleep_ms(60);                      /* 회수 스캔이 몇 번 돌 시간: 잘못된 구현이면 여기서 해제된다 */
    CHECK(stat_of(adm, "limbo") == 1 && stat_of(adm, "freed") == freed0);
    (void)cl_is(adm, "RESUME\n", "OK");
    char b[64];
    CHECK(cl_line(fd, b, sizeof b) && !strcmp(b, "CFG 2 21 42 gen2"));   /* 시작할 때의 버전을 끝까지 본다 */
    CHECK(wait_stat(adm, "limbo", 0, 5000) && stat_of(adm, "freed") == freed0 + 1);
    CHECK(cl_is(fd, "CFG\n", "CFG 3 33 66 gen3"));

    /* 부하 + 연속 리로드: 찢어진 설정(beta != 2*alpha)이 한 번도 안 보인다 */
    int torn = 0;
    int v = 4;
    for (int round = 0; round < 30; round++, v++) {
        write_cfg(v, v * 7, 900);
        kill(h.pid, SIGHUP);
        (void)wait_stat(adm, "cfg", v, 5000);      /* 시그널은 합쳐질 수 있으니 1 개씩 끝나길 기다린다 */
        for (int k = 0; k < 8; k++) {
            int ver = 0, al = 0, be = 0;
            char nm[16];
            if (!cl_req(fd, "CFG\n", b, sizeof b) || sscanf(b, "CFG %d %d %d %15s", &ver, &al, &be, nm) != 4 || be != 2 * al) torn++;
        }
    }
    CHECK(torn == 0);
    CHECK(wait_stat(adm, "limbo", 0, 5000) && stat_of(adm, "reloads") == 32);
    close(fd); close(adm);
    kill(h.pid, SIGTERM);
    CHECK(srv_wait(&h, 8000) == 0);
}

/* ---- Part C ---- */
static long file_size(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }
static void append_bytes(const char *p, const void *b, size_t n) {
    int fd = open(p, O_WRONLY | O_APPEND);
    if (fd >= 0) { if (write(fd, b, n) < 0) { /* 시험 도구: 무시 */ } close(fd); }
}

static void test_recover(void) {
    srvh h;
    reset_state();
    bool up = srv_start(&h, 2, 16, "none", 64, 3000);
    CHECK(up);
    if (!up) { if (h.alive) srv_sigkill(&h); return; }
    CHECK(h.rec == 0);
    int fd = cl_connect();
    for (int i = 0; i < 10; i++) { char m[32]; snprintf(m, sizeof m, "SET k%d %d\n", i, i); (void)cl_is(fd, m, "OK"); }
    (void)cl_is(fd, "DEL k3\n", "OK");
    (void)cl_is(fd, "SET k4 overwritten\n", "OK");
    close(fd);
    kill(h.pid, SIGTERM);
    (void)srv_wait(&h, 8000);
    long size_good = file_size(g_log);
    CHECK(size_good > 0);

#define RESTART_CHECK(expect_rec, expect_trunc, label) do { \
        srvh r; bool u = srv_start(&r, 2, 16, "none", 64, 3000); \
        CHECK(u && r.rec == (expect_rec) && r.trunc == (expect_trunc)); \
        if (u) { label; kill(r.pid, SIGTERM); (void)srv_wait(&r, 8000); } else if (r.alive) srv_sigkill(&r); \
    } while (0)

    /* 정상 재시작: DEL 과 덮어쓰기까지 재현 */
    RESTART_CHECK(12, 0, {
        int c = cl_connect();
        CHECK(cl_is(c, "GET k3\n", "NF"));
        CHECK(cl_is(c, "GET k4\n", "VAL overwritten"));
        CHECK(cl_is(c, "GET k9\n", "VAL 9"));
        close(c);
    });
    /* 두 번 재시작해도 결과가 같다 (복구는 멱등) */
    RESTART_CHECK(12, 0, { });

    /* 찢어진 꼬리 1: 머리 일부만 기록된 상태 → 그 앞까지만 복구, 파일은 잘린다 */
    uint8_t tail[WAL_REC_MAX];
    size_t n = wal_encode(tail, OP_SET, "torn", 4, "value", 5);
    append_bytes(g_log, tail, 7);
    RESTART_CHECK(12, 7, { });
    CHECK(file_size(g_log) == size_good);
    /* 찢어진 꼬리 2: 머리는 다 오고 몸통이 중간에 잘림 */
    append_bytes(g_log, tail, n - 3);
    RESTART_CHECK(12, (long long)(n - 3), {
        int c = cl_connect();
        CHECK(cl_is(c, "GET torn\n", "NF"));
        close(c);
    });
    CHECK(file_size(g_log) == size_good);
    /* 온전한 레코드를 붙이면 복구된다 (도구 자체 검증) */
    append_bytes(g_log, tail, n);
    RESTART_CHECK(13, 0, {
        int c = cl_connect();
        CHECK(cl_is(c, "GET torn\n", "VAL value"));
        close(c);
    });

    /* 중간 비트 깨짐: 두 번째 레코드의 몸통 1 비트를 뒤집는다 → CRC 가 잡고 거기서 멈춘다 */
    int rw = open(g_log, O_RDWR);
    uint32_t plen = 0;
    if (rw >= 0) {
        if (pread(rw, &plen, 4, 4) == 4) {
            off_t second = WAL_HDR + (off_t)plen;
            uint8_t b = 0;
            if (pread(rw, &b, 1, second + WAL_HDR + 6) == 1) { b ^= 0x40; if (pwrite(rw, &b, 1, second + WAL_HDR + 6) != 1) { /* 무시 */ } }
        }
        close(rw);
    }
    long long before_flip = file_size(g_log);
    RESTART_CHECK(1, before_flip - (long long)(WAL_HDR + plen), {
        int c = cl_connect();
        CHECK(cl_is(c, "GET k0\n", "VAL 0"));
        CHECK(cl_is(c, "GET k1\n", "NF"));
        close(c);
    });
}

static void test_sigkill(void) {
    srvh h;
    dl_arg arg[4];
    reset_state();
    bool up = srv_start(&h, 4, 16, "none", 256, 3000);
    CHECK(up);
    if (!up) { if (h.alive) srv_sigkill(&h); return; }
    run_load_until(&h, arg, 300, SIGKILL);
    waitpid(h.pid, &h.status, 0);
    h.alive = false; g_child = 0;
    close(h.out);
    long ok = 0;
    for (int i = 0; i < 4; i++) ok += arg[i].last_ok + 1;
    CHECK(WIFSIGNALED(h.status) && WTERMSIG(h.status) == SIGKILL);
    srvh h2;
    up = srv_start(&h2, 2, 16, "none", 256, 3000);
    CHECK(up);
    printf("      SIGKILL: ack 받은 쓰기 %ld 건, 재시작 복구 %lld 건\n", ok, up ? h2.rec : -1LL);
    if (up) {
        CHECK(h2.rec >= ok);           /* ack 는 로그 뒤에 나간다 */
        CHECK(count_missing(arg) == 0);
        kill(h2.pid, SIGTERM);
        CHECK(srv_wait(&h2, 8000) == 0);
    } else {
        if (h2.alive) srv_sigkill(&h2);
        skip(3, "재시작 실패");
    }
}

/* ---- --scenario: 일부러 만든 버그를 ASan 으로 보는 데모 ---- */
static int scenario_free_now(void) {
    srvh h;
    reset_state();
    if (!srv_start(&h, 1, 8, "free-now", 64, 3000)) return 2;
    int adm = cl_connect(), fd = cl_connect();
    write_cfg(2, 21, 900);
    (void)cl_is(adm, "PAUSE\n", "OK");
    (void)cl_send(fd, "CFG\n");
    (void)wait_stat(adm, "held", 1, 5000);
    kill(h.pid, SIGHUP);
    (void)wait_stat(adm, "cfg", 2, 5000);
    (void)cl_is(adm, "RESUME\n", "OK");
    char b[64] = "";
    bool got = cl_line(fd, b, sizeof b);
    printf("scenario free-now: 응답 받음=%d, 내용=\"%s\"\n", got, b);
    close(fd); close(adm);
    int rc = srv_wait(&h, 3000);
    printf("scenario free-now: 서버 종료 코드 %d, 시그널 %s\n", rc, WIFSIGNALED(h.status) ? strsignal(WTERMSIG(h.status)) : "(없음)");
    return 0;
}

static void cleanup_dir(void) {
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s.tmp", g_cfg);
    unlink(g_sock); unlink(g_log); unlink(g_cfg); unlink(tmp);
    rmdir(g_dir);
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--server")) return server_main(argc, argv);

    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGALRM, on_watchdog);
    alarm(280);
    uint32_t sz = sizeof g_exe;
    if (_NSGetExecutablePath(g_exe, &sz) != 0) return 2;
    snprintf(g_dir, sizeof g_dir, "/tmp/osa11.XXXXXX");
    if (!mkdtemp(g_dir)) { perror("mkdtemp"); return 2; }
    snprintf(g_sock, sizeof g_sock, "%s/s.sock", g_dir);
    snprintf(g_log, sizeof g_log, "%s/wal.log", g_dir);
    snprintf(g_cfg, sizeof g_cfg, "%s/kv.cfg", g_dir);
    atexit(cleanup_dir);

    if (argc > 2 && !strcmp(argv[1], "--scenario") && !strcmp(argv[2], "free-now")) return scenario_free_now();

    printf("--- Part A: slab, bounded queue ---\n");
    bool a1 = test_slab();
    bool a2 = test_bq();
    bool a = false;
    printf("--- Part A: 서버 (이벤트 루프 + 워커) ---\n");
    if (a1 && a2) a = test_server_a();
    else skip(24, "Part A 단위 실패");

    printf("--- Part B: drain ---\n");
    if (a) {
        drain_scenario("none", true);
        drain_scenario("drop-queue", false);      /* 검사가 진짜로 버그를 잡는지 (mutation) */
        test_drain_under_load();
    } else skip(25, "Part A 실패");
    printf("--- Part B: 설정 리로드 ---\n");
    if (a) test_reload();
    else skip(15, "Part A 실패");

    printf("--- Part C: 로그 복구 ---\n");
    if (a) { test_recover(); test_sigkill(); }
    else skip(24, "Part A 실패");

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
