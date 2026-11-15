/* ALGO-A12 메모리 제약 외부 정렬: 4KB RAM으로 대용량 정렬
 *
 * 모델: 4KB RAM 의 MCU 가 SPI NOR 같은 Flash 에 쌓인 16 B 레코드 N 개(최대 1M = 16 MiB)를 key 로 정렬한다.
 *   RAM   = static g_ram[4096] 하나. 데이터용 버퍼·힙·상태 배열은 전부 ram_alloc() 으로 여기서만 잘라 쓴다.
 *           예산을 넘기면 ram_alloc 이 abort() — "코드로 강제". 스택에는 스칼라 지역 변수만 둔다.
 *   Flash = 파일 하나 (경로는 argv[1], 없으면 $TMPDIR/algo_a12_flash.bin). 256 B 블록 단위
 *           pread/pwrite 만 쓰고 블록 수를 센다. stdio(fread)는 자기 버퍼(보통 수 KB)를 RAM 밖에 두므로 쓰지 않는다.
 *   Part A  런 생성 2가지: load-sort-write(4KB 를 채워 안정 삽입 정렬) / 교체 선택(replacement selection, 힙)
 *   Part B  힙 기반 k-way 병합 패스(k = RAM 이 허락하는 최대 13) + 패스 수 이론값 ⌈log_k R⌉ (정수로)
 *   Part C  1M 레코드 센서 로그: 읽기/쓰기 바이트 · 패스 수를 이론값과 정확히 비교
 *
 * 테스트 쪽(하네스) 메모리는 예산에 넣지 않는다 (호스트가 검증하는 것). 모든 테스트는 결정적(xorshift32 고정 시드).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st "$SCRATCH/flash.bin"   (solution.c 도 같은 방식)
 */
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ======================= 레코드 · 상수 ======================= */
typedef struct { uint32_t key, seq, pay0, pay1; } rec_t;    /* seq = 원래 위치(안정성 검사용), pay = 무결성 검사용 */
_Static_assert(sizeof(rec_t) == 16, "record must be 16 B");

#define RAM_BUDGET 4096u
#define BLK        256u                                    /* Flash I/O 단위 (SPI NOR page program 크기) */
#define RPB        (BLK / (uint32_t)sizeof(rec_t))         /* 블록당 레코드 16 */
#define DPB        (BLK / 4u)                              /* 블록당 런 디렉터리 항목(uint32 길이) 64 */
#define NBLK(n)    (((n) + RPB - 1) / RPB)                 /* 레코드 n 개가 차지하는 블록 수 */

/* ======================= RAM: 4 KB 아레나 (예산 강제) ======================= */
static _Alignas(16) uint8_t g_ram[RAM_BUDGET];
static size_t g_ram_top, g_ram_peak;

static void *ram_alloc(size_t n) {
    size_t a = (g_ram_top + 7u) & ~(size_t)7u;             /* 8 B 정렬 */
    if (n > RAM_BUDGET || a > RAM_BUDGET - n) {
        fprintf(stderr, "RAM budget exceeded: top=%zu + %zu > %u\n", a, n, RAM_BUDGET);
        abort();
    }
    g_ram_top = a + n;
    if (g_ram_top > g_ram_peak) g_ram_peak = g_ram_top;
    return &g_ram[a];
}
static size_t ram_mark(void) { return g_ram_top; }
static void ram_release(size_t m) { g_ram_top = m; }
static size_t ram_round8(size_t n) { return (n + 7u) & ~(size_t)7u; }

/* ======================= Flash 모델 (파일 + 블록 카운터) ======================= */
enum { IO_DATA, IO_META };
typedef struct {
    int fd;
    uint32_t nblocks;                       /* 장치 크기 (블록) */
    uint64_t rd[2], wr[2];                  /* [IO_DATA] 레코드 블록, [IO_META] 런 디렉터리 블록 */
    int err;                                /* 한 번이라도 I/O 오류 / 범위 밖 접근 → 1 */
} flash_t;

static int flash_read(flash_t *f, uint32_t blk, void *buf, int kind) {
    if (blk >= f->nblocks) { f->err = 1; return -1; }
    if (pread(f->fd, buf, BLK, (off_t)blk * BLK) != (ssize_t)BLK) { f->err = 1; return -1; }
    f->rd[kind]++;
    return 0;
}
static int flash_write(flash_t *f, uint32_t blk, const void *buf, int kind) {
    if (blk >= f->nblocks) { f->err = 1; return -1; }
    if (pwrite(f->fd, buf, BLK, (off_t)blk * BLK) != (ssize_t)BLK) { f->err = 1; return -1; }
    f->wr[kind]++;
    return 0;
}

/* 정렬기 문맥: 입력 영역 + 런 영역 2개 + 디렉터리 2개 (패스마다 0 ↔ 1 핑퐁) */
typedef struct {
    flash_t *f;
    uint32_t n;                             /* 입력 레코드 수 */
    uint32_t in0;                           /* 입력 시작 블록 (NBLK(n) 블록, 마지막 블록 남는 칸은 0xFF) */
    uint32_t area[2], dir[2];               /* 런 데이터 / 런 길이 디렉터리 시작 블록 */
} xs_t;

/* ---- 런 쓰기: 블록 하나짜리 출력 버퍼. 런은 항상 블록 경계에서 시작 (끝 블록 남는 칸 = 0xFF, 지운 Flash 값) ---- */
typedef struct { flash_t *f; uint32_t next; rec_t *buf; uint32_t fill; } runw_t;
static int runw_put(runw_t *w, const rec_t *r) {
    w->buf[w->fill++] = *r;
    if (w->fill == RPB) { w->fill = 0; return flash_write(w->f, w->next++, w->buf, IO_DATA); }
    return 0;
}
static int runw_end(runw_t *w) {
    if (w->fill == 0) return 0;
    memset(&w->buf[w->fill], 0xFF, (RPB - w->fill) * sizeof(rec_t));
    w->fill = 0;
    return flash_write(w->f, w->next++, w->buf, IO_DATA);
}

/* ---- 디렉터리 쓰기: 런 길이(uint32) 를 차례로 붙인다 ---- */
typedef struct { flash_t *f; uint32_t base, n; uint32_t *buf; } dirw_t;
static int dirw_put(dirw_t *w, uint32_t len) {
    w->buf[w->n % DPB] = len;
    w->n++;
    if (w->n % DPB == 0) return flash_write(w->f, w->base + w->n / DPB - 1, w->buf, IO_META);
    return 0;
}
static int dirw_flush(dirw_t *w) {
    if (w->n % DPB == 0) return 0;
    for (uint32_t i = w->n % DPB; i < DPB; i++) w->buf[i] = 0xFFFFFFFFu;
    return flash_write(w->f, w->base + w->n / DPB, w->buf, IO_META);
}

/* ---- 입력 스트림 읽기 (교체 선택용): 블록 버퍼 하나 ---- */
typedef struct { flash_t *f; uint32_t next, left; rec_t *buf; uint32_t pos, cnt; } inr_t;
static int inr_get(inr_t *r, rec_t *out) {               /* 1 = 하나 읽음, 0 = 끝, -1 = 오류 */
    if (r->pos == r->cnt) {
        if (r->left == 0) return 0;
        if (flash_read(r->f, r->next++, r->buf, IO_DATA) != 0) return -1;
        r->cnt = r->left < RPB ? r->left : RPB;
        r->left -= r->cnt;
        r->pos = 0;
    }
    *out = r->buf[r->pos++];
    return 1;
}

/* ---- 레코드 힙 (교체 선택용 최소 힙, key 만 비교 — DS-B12에서 자세히) ---- */
static void rheap_down(rec_t *h, uint32_t n, uint32_t i) {
    rec_t x = h[i];
    for (;;) {
        uint32_t c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && h[c + 1].key < h[c].key) c++;
        if (h[c].key >= x.key) break;
        h[i] = h[c];
        i = c;
    }
    h[i] = x;
}
static void rheap_build(rec_t *h, uint32_t n) {
    for (uint32_t i = n / 2; i-- > 0;) rheap_down(h, n, i);
}

/* 교체 선택이 쓰는 RAM: 입력·출력·디렉터리 블록 버퍼 3개 + 나머지 전부 레코드 힙 */
static uint32_t rs_capacity(void) { return (RAM_BUDGET - 3 * BLK) / (uint32_t)sizeof(rec_t); }   /* 208 */

/* k-way 병합 한 그룹의 런 상태: 다음에 읽을 블록, 아직 안 읽은 레코드 수, 블록 버퍼 안 위치/개수 */
typedef struct { uint32_t next, left; uint16_t pos, cnt; } mrun_t;

/* 병합 패스가 ram_alloc 으로 실제로 잘라 쓰는 바이트 (merge_pass 의 할당 순서와 똑같이 계산) */
static size_t merge_ram_need(uint32_t k) {
    return ram_round8((size_t)k * BLK) + BLK + BLK          /* 입력 블록 k 개 + 출력 블록 + 디렉터리 블록 */
         + ram_round8((size_t)k * sizeof(mrun_t)) + ram_round8((size_t)k * sizeof(uint16_t));
}
static uint32_t merge_fanin_max(void) {
    uint32_t k = 2;
    while (merge_ram_need(k + 1) <= RAM_BUDGET) k++;
    return k;                                                /* 4 KB, 256 B 블록 → 13 */
}

/* ======================= Part A — 런 생성 ======================= */

/* RAM 안 정렬: key 오름차순, 같은 key 는 원래 순서 유지(안정). 추가 메모리 0. */
static void sort_run(rec_t *a, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) {
        rec_t x = a[i];
        uint32_t j = i;
        while (j > 0 && a[j - 1].key > x.key) { a[j] = a[j - 1]; j--; }   /* '>' : 같은 key 는 넘지 않는다 */
        a[j] = x;
    }
}

/* load-sort-write: 4 KB 전부(256 레코드)를 입력으로 채워 sort_run → 그대로 area[0] 에 쓴다.
 * 디렉터리는 런을 다 쓴 뒤 같은 RAM 을 재사용해 dir[0] 에 쓴다. 반환 = 런 수, 오류 = -1. */
static int64_t gen_runs_lsw(xs_t *x) {
    size_t mark = ram_mark();
    const uint32_t L = RAM_BUDGET / (uint32_t)sizeof(rec_t);
    rec_t *a = ram_alloc(L * sizeof(rec_t));
    uint32_t in = x->in0, out = x->area[0], left = x->n, runs = 0;
    while (left > 0) {
        uint32_t n = left < L ? left : L, nb = NBLK(n);
        for (uint32_t b = 0; b < nb; b++)
            if (flash_read(x->f, in++, &a[b * RPB], IO_DATA) != 0) goto fail;
        sort_run(a, n);
        memset(&a[n], 0xFF, (nb * RPB - n) * sizeof(rec_t));
        for (uint32_t b = 0; b < nb; b++)
            if (flash_write(x->f, out++, &a[b * RPB], IO_DATA) != 0) goto fail;
        left -= n;
        runs++;
    }
    ram_release(mark);
    {
        dirw_t dw = { x->f, x->dir[0], 0, ram_alloc(BLK) };
        for (uint32_t r = 0; r < runs; r++) {
            uint32_t len = (r + 1 < runs || x->n % L == 0) ? L : x->n % L;
            if (dirw_put(&dw, len) != 0) goto fail;
        }
        if (dirw_flush(&dw) != 0) goto fail;
    }
    ram_release(mark);
    return runs;
fail:
    ram_release(mark);
    return -1;
}

/* 교체 선택: h[0..hn) = 이번 런 힙, h[hn..n) = 다음 런으로 얼린(frozen) 레코드. 둘이 한 배열을 나눠 쓴다.
 * 루트를 내보내고(last), 새 레코드 x 가 x.key >= last 면 루트 자리에, 아니면 힙을 하나 줄이고 꼬리에 얼린다.
 * hn == 0 이면 런 끝: 얼린 것 전부로 힙을 다시 만든다. 반환 = 런 수, 오류 = -1. */
static int64_t gen_runs_rs(xs_t *x) {
    size_t mark = ram_mark();
    rec_t *ib = ram_alloc(BLK), *ob = ram_alloc(BLK);
    uint32_t *db = ram_alloc(BLK);
    const uint32_t c = rs_capacity();
    rec_t *h = ram_alloc(c * sizeof(rec_t));
    inr_t in = { x->f, x->in0, x->n, ib, 0, 0 };
    runw_t ow = { x->f, x->area[0], ob, 0 };
    dirw_t dw = { x->f, x->dir[0], 0, db };
    uint32_t n = 0, hn, runs = 0, run_len = 0;
    int got;
    while (n < c && (got = inr_get(&in, &h[n])) == 1) n++;
    if (x->f->err) goto fail;
    rheap_build(h, n);
    hn = n;
    while (n > 0) {
        if (hn == 0) {                                      /* 이번 런 끝 → 얼린 것들로 새 런 */
            if (runw_end(&ow) != 0 || dirw_put(&dw, run_len) != 0) goto fail;
            runs++; run_len = 0;
            hn = n;
            rheap_build(h, hn);
        }
        uint32_t last = h[0].key;
        if (runw_put(&ow, &h[0]) != 0) goto fail;
        run_len++;
        rec_t nx;
        got = inr_get(&in, &nx);
        if (got < 0) goto fail;
        if (got == 1 && nx.key >= last) {                   /* 이번 런에 들어갈 수 있다 */
            h[0] = nx;
            rheap_down(h, hn, 0);
        } else {                                            /* 힙을 하나 줄인다 */
            h[0] = h[hn - 1];
            hn--;
            rheap_down(h, hn, 0);
            if (got == 1) h[hn] = nx;                       /* 비운 칸에 얼린다 (n 그대로) */
            else { h[hn] = h[n - 1]; n--; }                 /* 입력 끝: 얼린 구역 맨 끝으로 구멍을 메운다 */
        }
    }
    if (run_len > 0) {
        if (runw_end(&ow) != 0 || dirw_put(&dw, run_len) != 0) goto fail;
        runs++;
    }
    if (dirw_flush(&dw) != 0) goto fail;
    ram_release(mark);
    return runs;
fail:
    ram_release(mark);
    return -1;
}

/* ======================= Part B — k-way 병합 패스 ======================= */

/* 힙 원소 = 그룹 안 런 번호 j. 비교 = (그 런의 현재 머리 key, j) — 같은 key 면 앞 런 먼저 → 안정 */
static int mless(const rec_t *ib, const mrun_t *st, uint16_t a, uint16_t b) {
    uint32_t ka = ib[a * RPB + st[a].pos].key, kb = ib[b * RPB + st[b].pos].key;
    return ka < kb || (ka == kb && a < b);
}
static void mheap_down(uint16_t *hp, uint32_t n, uint32_t i, const rec_t *ib, const mrun_t *st) {
    uint16_t x = hp[i];
    for (;;) {
        uint32_t c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && mless(ib, st, hp[c + 1], hp[c])) c++;
        if (!mless(ib, st, hp[c], x)) break;
        hp[i] = hp[c];
        i = c;
    }
    hp[i] = x;
}
static int mrun_load(flash_t *f, mrun_t *s, rec_t *buf) {
    if (flash_read(f, s->next++, buf, IO_DATA) != 0) return -1;
    s->cnt = (uint16_t)(s->left < RPB ? s->left : RPB);
    s->left -= s->cnt;
    s->pos = 0;
    return 0;
}

/* area[src]/dir[src] 의 런 R 개를 k 개씩 묶어 병합 → area[src^1]/dir[src^1]. 반환 = 새 런 수 ⌈R/k⌉, 오류 = -1.
 * 런은 디렉터리 순서대로 연속(블록 경계 정렬)해 있으므로 시작 블록 = 앞 런들의 NBLK 누적합. */
static int64_t merge_pass(xs_t *x, int src, uint32_t R, uint32_t k) {
    if (k < 2 || merge_ram_need(k) > RAM_BUDGET) return -1;
    size_t mark = ram_mark();
    rec_t *ib = ram_alloc((size_t)k * BLK);
    rec_t *ob = ram_alloc(BLK);
    uint32_t *db = ram_alloc(BLK);
    mrun_t *st = ram_alloc((size_t)k * sizeof(mrun_t));
    uint16_t *hp = ram_alloc((size_t)k * sizeof(uint16_t));
    int dst = src ^ 1;
    runw_t ow = { x->f, x->area[dst], ob, 0 };
    dirw_t dw = { x->f, x->dir[dst], 0, db };
    uint32_t src_blk = x->area[src], newR = 0;
    for (uint32_t g = 0; g < R; g += k) {
        uint32_t m = R - g < k ? R - g : k, cached = UINT32_MAX, total = 0, hn = 0;
        for (uint32_t j = 0; j < m; j++) {                  /* 런 길이: 디렉터리 블록을 (비어 있는) 출력 버퍼로 읽는다 */
            uint32_t idx = g + j, len;
            if (idx / DPB != cached) {
                cached = idx / DPB;
                if (flash_read(x->f, x->dir[src] + cached, ob, IO_META) != 0) goto fail;
            }
            memcpy(&len, (const uint8_t *)ob + 4u * (idx % DPB), sizeof len);
            st[j].next = src_blk; st[j].left = len; st[j].pos = st[j].cnt = 0;
            src_blk += NBLK(len);
            total += len;
        }
        for (uint32_t j = 0; j < m; j++)                    /* 머리 블록 읽고 힙에 넣기 */
            if (st[j].left > 0) {
                if (mrun_load(x->f, &st[j], &ib[j * RPB]) != 0) goto fail;
                hp[hn++] = (uint16_t)j;
            }
        for (uint32_t i = hn / 2; i-- > 0;) mheap_down(hp, hn, i, ib, st);
        while (hn > 0) {
            uint16_t j = hp[0];
            if (runw_put(&ow, &ib[j * RPB + st[j].pos]) != 0) goto fail;
            if (++st[j].pos == st[j].cnt) {
                if (st[j].left > 0) { if (mrun_load(x->f, &st[j], &ib[j * RPB]) != 0) goto fail; }
                else hp[0] = hp[--hn];                      /* 런 j 소진 → 힙에서 뺀다 */
            }
            if (hn > 0) mheap_down(hp, hn, 0, ib, st);
        }
        if (runw_end(&ow) != 0 || dirw_put(&dw, total) != 0) goto fail;
        newR++;
    }
    if (dirw_flush(&dw) != 0) goto fail;
    ram_release(mark);
    return newR;
fail:
    ram_release(mark);
    return -1;
}

/* 패스 수 이론값: k^P >= R 인 가장 작은 P (R <= 1 이면 0). 부동소수 log 금지 — 정수로. */
static uint32_t passes_theory(uint64_t R, uint32_t k) {
    uint32_t p = 0;
    uint64_t cap = 1;
    while (cap < R) { cap *= k; p++; }
    return p;
}

/* ======================= 드라이버 (주어짐) ======================= */
enum { M_LSW, M_RS };
typedef struct {
    uint32_t runs0, passes, k;
    int out;                                 /* 결과가 있는 영역 0/1 (런 1개). 런 0개(n=0)면 -1 */
    int64_t err;
} xs_result_t;

static xs_result_t ext_sort(xs_t *x, int method, uint32_t k) {
    xs_result_t r = { 0, 0, k, -1, 0 };
    int64_t R = method == M_LSW ? gen_runs_lsw(x) : gen_runs_rs(x);
    if (R < 0) { r.err = -1; return r; }
    r.runs0 = (uint32_t)R;
    int src = 0;
    while (R > 1) {
        int64_t nr = merge_pass(x, src, (uint32_t)R, k);
        if (nr < 0 || nr >= R) { r.err = -2; return r; }   /* 줄지 않으면 무한 루프 → 중단 */
        R = nr; src ^= 1; r.passes++;
    }
    r.out = R == 1 ? src : -1;
    return r;
}

/* ======================= 테스트 하네스 (호스트 쪽, RAM 예산 밖) ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static uint64_t mix64(uint64_t z) {                           /* splitmix64 마무리 함수 */
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static rec_t mkrec(uint32_t key, uint32_t seq) {
    uint32_t p = (uint32_t)mix64(((uint64_t)key << 32) | seq);
    rec_t r = { key, seq, p, ~p ^ seq };
    return r;
}
static int rec_ok(const rec_t *r) { return r->pay0 == (uint32_t)mix64(((uint64_t)r->key << 32) | r->seq) && r->pay1 == (~r->pay0 ^ r->seq); }
static uint64_t rec_hash(const rec_t *r) {                    /* 순서 무관 multiset 해시 = 합 (XOR 아님) */
    return mix64(((uint64_t)r->key << 32) | r->seq) + mix64(((uint64_t)r->pay0 << 32) | r->pay1);
}

enum { G_RANDOM, G_DUPS, G_SORTED, G_REVERSE, G_EQUAL, G_EXTREME, G_NEARLY, G_NKIND };
static const char *GNAME[] = { "random", "dups", "sorted", "reverse", "equal", "extreme", "nearly" };
/* G_NEARLY: key = 16·i + r, r ∈ [0, 16·D) → 앞에 있는 더 큰 key 는 최대 D-1 개 (5. 풀이 Part B) */
static uint32_t gen_key(int kind, uint32_t i, uint32_t n, uint32_t param) {
    switch (kind) {
    case G_RANDOM:  return xs32();
    case G_DUPS:    return xs32() % (param ? param : 64);
    case G_SORTED:  return i / 3;
    case G_REVERSE: return n - i;
    case G_EQUAL:   return 7;
    case G_EXTREME: return (xs32() & 1) ? UINT32_MAX : 0;
    default:        return 16u * i + xs32() % (16u * (param ? param : 1));
    }
}

static flash_t g_f;
static const char *g_path;
static uint64_t g_in_hash;

/* 입력을 Flash 에 쓰고(카운터 밖) 영역을 배치한다. 런 영역 = 입력 블록 + 런 수 상한(블록 경계 낭비 ≤ 1 블록/런) */
static void setup(xs_t *x, int kind, uint32_t n, uint32_t param) {
    uint32_t nin = NBLK(n), rmax = n / 200 + 2;               /* 런 길이 ≥ 208 (마지막 둘 제외) */
    uint32_t narea = nin + rmax + 1, ndir = rmax / DPB + 2;
    x->f = &g_f; x->n = n; x->in0 = 0;
    x->area[0] = nin; x->area[1] = nin + narea;
    x->dir[0] = nin + 2 * narea; x->dir[1] = x->dir[0] + ndir;
    g_f.nblocks = x->dir[1] + ndir;
    memset(g_f.rd, 0, sizeof g_f.rd); memset(g_f.wr, 0, sizeof g_f.wr); g_f.err = 0;
    static rec_t b[16];
    g_in_hash = 0;
    for (uint32_t blk = 0; blk < nin; blk++) {
        memset(b, 0xFF, sizeof b);
        for (uint32_t j = 0; j < RPB && blk * RPB + j < n; j++) {
            uint32_t i = blk * RPB + j;
            b[j] = mkrec(gen_key(kind, i, n, param), i);
            g_in_hash += rec_hash(&b[j]);
        }
        if (pwrite(g_f.fd, b, BLK, (off_t)blk * BLK) != (ssize_t)BLK) g_f.err = 1;
    }
}
static uint32_t peek_dir(const xs_t *x, int area, uint32_t i) {   /* 카운터 밖 읽기 (검증용) */
    uint32_t v = 0;
    if (pread(g_f.fd, &v, 4, (off_t)(x->dir[area] + i / DPB) * BLK + 4 * (i % DPB)) != 4) return UINT32_MAX;
    return v;
}
static uint64_t sum_blocks(const xs_t *x, int area, uint32_t R) { /* Σ ⌈len/16⌉ — 한 패스가 읽고 쓰는 데이터 블록 */
    uint64_t s = 0;
    for (uint32_t i = 0; i < R; i++) s += NBLK(peek_dir(x, area, i));
    return s;
}

typedef struct { int sorted, stable, perm, payload; uint32_t count; uint64_t unstable_pairs; } vres_t;
static rec_t g_ref[6000], g_out[6000];
static vres_t verify(const xs_t *x, const xs_result_t *r, int collect) {
    vres_t v = { 1, 1, 0, 1, 0, 0 };
    uint64_t h = 0;
    if (r->out < 0) { v.perm = (x->n == 0 && r->err == 0); return v; }
    uint32_t len = peek_dir(x, r->out, 0);
    static rec_t b[16];
    rec_t prev = { 0, 0, 0, 0 };
    for (uint32_t i = 0; i < len && i < x->n + 1; i++) {
        if (i % RPB == 0 && pread(g_f.fd, b, BLK, (off_t)(x->area[r->out] + i / RPB) * BLK) != (ssize_t)BLK) { v.sorted = 0; break; }
        rec_t c = b[i % RPB];
        if (!rec_ok(&c)) v.payload = 0;
        if (i > 0 && c.key < prev.key) v.sorted = 0;
        if (i > 0 && c.key == prev.key && c.seq < prev.seq) { v.stable = 0; v.unstable_pairs++; }
        h += rec_hash(&c);
        if (collect && i < 6000) g_out[i] = c;
        prev = c;
        v.count++;
    }
    v.perm = v.count == x->n && h == g_in_hash;
    return v;
}
static int cmp_ref(const void *a, const void *b) {
    const rec_t *p = a, *q = b;
    if (p->key != q->key) return p->key < q->key ? -1 : 1;
    return p->seq < q->seq ? -1 : p->seq > q->seq;
}
/* 작은 n: 입력을 다시 만들어 qsort((key, seq)) 결과와 비교. exact = 레코드 전체 일치(안정), 아니면 key 열만 */
static int ref_match(int kind, uint32_t n, uint32_t param, uint32_t seed, int exact) {
    g_rng = seed;
    for (uint32_t i = 0; i < n; i++) g_ref[i] = mkrec(gen_key(kind, i, n, param), i);
    qsort(g_ref, n, sizeof(rec_t), cmp_ref);
    for (uint32_t i = 0; i < n; i++) {
        if (g_ref[i].key != g_out[i].key) return 0;
        if (exact && g_ref[i].seq != g_out[i].seq) return 0;
    }
    return 1;
}

/* 한 번 실행: 단계마다 측정 블록 수 == 디렉터리에서 계산한 기대값인지 확인 (traffic_ok) */
typedef struct { xs_result_t r; vres_t v; int traffic_ok; uint64_t rd, wr, rdm, wrm; size_t peak; } run_t;
static run_t run_once(int kind, uint32_t n, uint32_t param, uint32_t seed, int method, uint32_t k, int collect) {
    run_t o;
    memset(&o, 0, sizeof o);
    xs_t x;
    g_rng = seed;
    setup(&x, kind, n, param);
    g_ram_top = 0; g_ram_peak = 0;
    o.traffic_ok = 1;
    int64_t R = method == M_LSW ? gen_runs_lsw(&x) : gen_runs_rs(&x);
    o.r.k = k; o.r.out = -1;
    if (R < 0) { o.r.err = -1; o.traffic_ok = 0; return o; }
    o.r.runs0 = (uint32_t)R;
    uint64_t exp_rd = method == M_LSW ? sum_blocks(&x, 0, (uint32_t)R) : NBLK(n);
    if (g_f.rd[IO_DATA] != exp_rd || g_f.wr[IO_DATA] != sum_blocks(&x, 0, (uint32_t)R)) o.traffic_ok = 0;
    int src = 0;
    while (R > 1) {
        uint64_t rd0 = g_f.rd[IO_DATA], wr0 = g_f.wr[IO_DATA], in_blocks = sum_blocks(&x, src, (uint32_t)R);
        int64_t nr = merge_pass(&x, src, (uint32_t)R, k);
        if (nr < 0 || nr >= R) { o.r.err = -2; o.traffic_ok = 0; break; }
        if (g_f.rd[IO_DATA] - rd0 != in_blocks || g_f.wr[IO_DATA] - wr0 != sum_blocks(&x, src ^ 1, (uint32_t)nr)) o.traffic_ok = 0;
        R = nr; src ^= 1; o.r.passes++;
    }
    if (o.r.err == 0) o.r.out = R == 1 ? src : -1;
    if (g_f.err) o.traffic_ok = 0;
    o.rd = g_f.rd[IO_DATA]; o.wr = g_f.wr[IO_DATA]; o.rdm = g_f.rd[IO_META]; o.wrm = g_f.wr[IO_META];
    o.peak = g_ram_peak;
    o.v = verify(&x, &o.r, collect);
    return o;
}
static int run_good(const run_t *o) {
    return o->r.err == 0 && o->v.sorted && o->v.perm && o->v.payload && o->traffic_ok && o->peak <= RAM_BUDGET
        && o->r.passes == passes_theory(o->r.runs0, o->r.k);
}

#ifndef ALGO_A12_NO_MAIN   /* demo_full.c 는 이 위까지(정렬기 + 검증 도구)를 가져다 쓴다 */
int main(int argc, char **argv) {
    static char path[1024];
    const char *tmp = getenv("TMPDIR");
    if (argc > 1) g_path = argv[1];
    else { snprintf(path, sizeof path, "%s/algo_a12_flash.bin", tmp && *tmp ? tmp : "/tmp"); g_path = path; }
    g_f.fd = open(g_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (g_f.fd < 0) { perror(g_path); return 2; }
    printf("flash file: %s\n", g_path);
    const uint32_t K = merge_fanin_max();

    /* ---------- Part A: 예산 · 패스 수 공식 · 안정 삽입 정렬 ---------- */
    printf("-- Part A: RAM budget %u B, block %u B, merge fan-in k=%u (needs %zu B), RS heap %u records\n",
           RAM_BUDGET, BLK, K, merge_ram_need(K), rs_capacity());
    CHECK(K == 13 && merge_ram_need(13) <= RAM_BUDGET && merge_ram_need(14) > RAM_BUDGET && rs_capacity() == 208);
    {
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {                                       /* 자식: 예산 초과 → abort 되어야 한다 */
            freopen("/dev/null", "w", stderr);
            g_ram_top = 0; (void)ram_alloc(4000); (void)ram_alloc(97);
            _exit(0);
        }
        int ws = 0;
        waitpid(pid, &ws, 0);
        CHECK(WIFSIGNALED(ws) && WTERMSIG(ws) == SIGABRT);
        g_ram_top = 0; (void)ram_alloc(4000); (void)ram_alloc(96);   /* 딱 4096 은 허용 */
        CHECK(g_ram_top == RAM_BUDGET);
        g_ram_top = 0;
    }
    CHECK(passes_theory(0, 13) == 0 && passes_theory(1, 13) == 0 && passes_theory(2, 2) == 1);
    CHECK(passes_theory(2197, 13) == 3 && passes_theory(2198, 13) == 4 && passes_theory(4096, 13) == 4);
    CHECK(passes_theory(125, 5) == 3 && passes_theory(216, 6) == 3 && passes_theory(16807, 7) == 5);  /* ceil(log) 이 틀리는 값들 */
    {
        int bad = 0;
        for (uint32_t k = 2; k <= 40; k++)
            for (uint32_t R = 1; R <= 5000; R++) {            /* 기준: R 을 ⌈R/k⌉ 로 줄여 1 이 될 때까지 센 횟수 */
                uint32_t p = 0, r = R;
                while (r > 1) { r = (r + k - 1) / k; p++; }
                if (passes_theory(R, k) != p) bad++;
            }
        CHECK(bad == 0);
    }
    {
        int bad = 0;
        for (int t = 0; t < 400; t++) {
            uint32_t n = t < 65 ? (uint32_t)t : 1 + xs32() % 256, mod = 1 + xs32() % 40;
            for (uint32_t i = 0; i < n; i++) { g_ref[i] = mkrec(xs32() % mod, i); g_out[i] = g_ref[i]; }
            sort_run(g_out, n);
            qsort(g_ref, n, sizeof(rec_t), cmp_ref);
            if (n && memcmp(g_ref, g_out, n * sizeof(rec_t)) != 0) bad++;
        }
        CHECK(bad == 0);                                      /* 안정: (key, seq) 정렬과 레코드 단위로 같다 */
    }

    /* ---------- Part A/B: 끝까지 — 경계 n, 두 방법, k=13 과 k=2 ---------- */
    {
        static const uint32_t NS[] = { 0, 1, 15, 16, 17, 207, 208, 209, 255, 256, 257, 4097 };
        int bad = 0;
        for (size_t i = 0; i < sizeof NS / sizeof NS[0]; i++)
            for (int m = 0; m < 2; m++)
                for (int kk = 0; kk < 2; kk++) {
                    run_t o = run_once(G_DUPS, NS[i], 50, 77u + (uint32_t)i, m, kk ? 2 : K, 1);
                    if (!run_good(&o) || !ref_match(G_DUPS, NS[i], 50, 77u + (uint32_t)i, m == M_LSW)
                        || (m == M_LSW && !o.v.stable)) {
                        if (++bad <= 4) printf("  edge n=%u %s k=%u: err=%lld sorted=%d perm=%d traffic=%d passes=%u/%u\n", NS[i],
                               m ? "RS" : "LSW", o.r.k, (long long)o.r.err, o.v.sorted, o.v.perm, o.traffic_ok,
                               o.r.passes, passes_theory(o.r.runs0, o.r.k));
                    }
                }
        CHECK(bad == 0);
    }
    {
        int bad = 0, cases = 0;
        uint64_t recs = 0;
        uint32_t seed = 1000;
        for (int t = 0; t < 120; t++) {
            int kind = (int)(xs32() % G_NKIND), m = t & 1;
            uint32_t n = xs32() % 5000, k = 2 + xs32() % (K - 1), param = 1 + xs32() % 300, s = seed++;
            run_t o = run_once(kind, n, param, s, m, k, 1);
            cases++; recs += n;
            if (!run_good(&o) || !ref_match(kind, n, param, s, m == M_LSW) || (m == M_LSW && !o.v.stable)) {
                bad++;
                if (bad <= 3) printf("  random case %d: %s n=%u %s k=%u FAILED\n", t, GNAME[kind], n, m ? "RS" : "LSW", k);
            }
        }
        printf("random: %d cases, %llu records, both methods, k in [2,%u]: mismatches=%d\n", cases,
               (unsigned long long)recs, K, bad);
        CHECK(bad == 0);
    }
    {                                                         /* 드라이버 ext_sort 그대로: 랜덤 3000, RS, k=3 */
        xs_t x;
        g_rng = 3; setup(&x, G_RANDOM, 3000, 0); g_ram_top = 0;
        xs_result_t r = ext_sort(&x, M_RS, 3);
        vres_t v = verify(&x, &r, 0);
        CHECK(r.err == 0 && r.passes == passes_theory(r.runs0, 3) && r.passes >= 2 && v.sorted && v.perm && v.payload);
    }

    /* ---------- Part A/B: 교체 선택의 런 개수 ---------- */
    {
        const uint32_t c = rs_capacity(), n = 100000;
        run_t s = run_once(G_SORTED, n, 0, 5, M_RS, K, 0);
        run_t r = run_once(G_REVERSE, n, 0, 5, M_RS, K, 0);
        run_t q = run_once(G_RANDOM, n, 0, 5, M_RS, K, 0);
        run_t l = run_once(G_RANDOM, n, 0, 5, M_LSW, K, 0);
        printf("runs (n=%u): LSW random %u | RS sorted %u, reverse %u, random %u (avg len %.1f = %.3f x heap %u)\n", n,
               l.r.runs0, s.r.runs0, r.r.runs0, q.r.runs0, q.r.runs0 ? (double)n / q.r.runs0 : 0.0, q.r.runs0 ? (double)n / q.r.runs0 / c : 0.0, c);
        CHECK(run_good(&s) && s.r.runs0 == 1 && s.r.passes == 0);
        CHECK(run_good(&r) && r.r.runs0 == (n + c - 1) / c);
        CHECK(run_good(&l) && l.r.runs0 == (n + 255) / 256);
        CHECK(run_good(&q) && q.r.runs0 < l.r.runs0);
        int each = 1;
        xs_t x;
        g_rng = 5; setup(&x, G_REVERSE, n, 0); g_ram_top = 0;
        int64_t R = gen_runs_rs(&x);
        for (int64_t i = 0; i + 1 < R; i++) if (peek_dir(&x, 0, (uint32_t)i) != c) each = 0;
        CHECK(R == (n + c - 1) / c && each);                  /* 역순: 마지막 빼고 모든 런이 정확히 c */
        run_t e1 = run_once(G_NEARLY, 50000, c, 9, M_RS, K, 0);       /* 앞의 더 큰 key ≤ c-1 개 → 런 1개 */
        CHECK(run_good(&e1) && e1.r.runs0 == 1);
        run_t eq = run_once(G_EQUAL, 50000, 0, 9, M_RS, K, 0);         /* 같은 key 는 이번 런에 (>=) → 런 1개 */
        CHECK(run_good(&eq) && eq.r.runs0 == 1);
        /* 딱 c 개 앞에 더 큰 key: 1..c, 0, c+1.. → 0 이 얼려져 런 2개 */
        g_rng = 1; setup(&x, G_SORTED, c + 10, 0);
        for (uint32_t i = 0; i < c + 10; i++) {
            rec_t rr = mkrec(i < c ? i + 1 : (i == c ? 0 : i), i);
            pwrite(g_f.fd, &rr, sizeof rr, (off_t)i * sizeof rr);
        }
        g_ram_top = 0;
        CHECK(gen_runs_rs(&x) == 2);
    }

    /* ---------- Part C: 1M 레코드 센서 로그 ---------- */
    {
        const uint32_t n = 1u << 20;
        const uint64_t D = (uint64_t)n * sizeof(rec_t);
        run_t a = run_once(G_DUPS, n, 1u << 18, 42, M_LSW, K, 0);
        printf("1M LSW  : runs=%u passes=%u (theory %u) read=%llu B write=%llu B  theory D(1+P) = %llu B each  meta r/w=%llu/%llu blk  RAM peak=%zu stable=%d\n",
               a.r.runs0, a.r.passes, passes_theory(a.r.runs0, K), (unsigned long long)(a.rd * BLK),
               (unsigned long long)(a.wr * BLK), (unsigned long long)(D * (1 + a.r.passes)),
               (unsigned long long)a.rdm, (unsigned long long)a.wrm, a.peak, a.v.stable);
        CHECK(run_good(&a) && a.v.stable && a.r.runs0 == 4096 && a.r.passes == 4);
        CHECK(a.rd * BLK == D * 5 && a.wr * BLK == D * 5);      /* 정렬된 런 길이가 16 의 배수 → 낭비 0, 공식과 정확히 같다 */
        /* 같은 1M 을 교체 선택으로: demo_full.c (시간 예산 때문에 여기선 생략, 결과는 md 5. 풀이 Part C) */
        run_t c = run_once(G_NEARLY, n, 8, 43, M_RS, K, 0);       /* 타임스탬프 지터 < 8 칸: 거의 정렬된 로그 */
        printf("1M RS nearly-sorted log: runs=%u passes=%u read=%llu B write=%llu B (= D each)\n",
               c.r.runs0, c.r.passes, (unsigned long long)(c.rd * BLK), (unsigned long long)(c.wr * BLK));
        CHECK(run_good(&c) && c.r.runs0 == 1 && c.rd * BLK == D && c.wr * BLK == D);
    }

    close(g_f.fd);
    unlink(g_path);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
