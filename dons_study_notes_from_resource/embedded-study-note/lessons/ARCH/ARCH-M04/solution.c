/* ARCH-M04 직접 사상(direct-mapped) 캐시 시뮬레이터
 *
 *   Part A  파라미터(C, B) 검사, tag/index/offset 분해, hit/miss 판정 + 통계 (compulsory, eviction)
 *           + 다른 방식(나눗셈/나머지)으로 짠 참조 모델과 결정적 랜덤 트레이스 대조
 *   Part B  라인 걸침 접근, 행렬 순회 트레이스 (행/열, pitch 패딩), stride 반복 루프,
 *           conflict miss 사례 (열 순회, C 간격 두 배열) 와 "워킹셋이 들어가면 나머지 미스는 전부 conflict"
 *   Part C  MCU 플래시 명령 캐시: 메인 루프와 ISR 의 set 충돌 검사 → 충돌 없는 ISR 배치 → fetch cycle 비교
 *
 * 공통 규칙 (ARCH-M04~M07 공유): 바이트 주소 uint32_t, C·B 는 2의 거듭제곱, W = 1 이므로 S = C / B,
 * offset 비트 = log2 B, index 비트 = log2 S, tag = 나머지 상위 비트. 트레이스 레코드 {addr, rw, size}.
 * 쓰기 정책은 모델링하지 않는다: 'W' 도 'R' 과 똑같이 조회하고 미스면 라인을 채운다
 * (write-allocate 가정. DRAM 트래픽·dirty 는 ARCH-M06).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DM_MAX_SETS 4096u      /* 4096 set 까지 (예: 128 KB / 32 B) */
#define LS_CAP      (1u << 14) /* compulsory 판정용 "본 라인" 집합 크기 (절반까지만 채운다) */

typedef struct { uint32_t addr; char rw; uint8_t size; } trace_rec_t;

/* ---------- 주어진 도구: 지금까지 본 라인 주소 집합 (열린 주소법 해시) ---------- */
typedef struct {
    uint32_t key[LS_CAP];
    uint8_t  used[LS_CAP];
    uint32_t count;
} lineset_t;

/* 1 = 처음 본 라인(새로 넣음), 0 = 이미 본 라인, -1 = 집합이 가득 참 (테스트는 넘지 않는다) */
static int ls_insert(lineset_t *s, uint32_t key) {
    uint32_t h = (key * 2654435761u) & (LS_CAP - 1);
    for (uint32_t p = 0; p < LS_CAP; p++) {
        uint32_t i = (h + p) & (LS_CAP - 1);
        if (!s->used[i]) {
            if (s->count >= LS_CAP / 2) return -1;
            s->used[i] = 1; s->key[i] = key; s->count++;
            return 1;
        }
        if (s->key[i] == key) return 0;
    }
    return -1;
}

typedef struct {
    uint64_t accesses, hits, misses;
    uint64_t compulsory;   /* 그 라인 주소를 처음 건드린 미스 (cold miss) */
    uint64_t evictions;    /* 유효한 라인을 밀어낸 미스 (빈 칸 채우기는 제외) */
    uint64_t reads, writes;
} dm_stats_t;

typedef struct {
    uint32_t capacity, line, sets;   /* C, B, S = C / B */
    unsigned off_bits, idx_bits;     /* log2 B, log2 S */
    uint8_t  valid[DM_MAX_SETS];
    uint32_t tag[DM_MAX_SETS];
    uint32_t set_evictions[DM_MAX_SETS];
    dm_stats_t st;
    lineset_t seen;
} dmcache_t;

static bool is_pow2(uint32_t x) { return x != 0 && (x & (x - 1)) == 0; }
static unsigned log2u(uint32_t x) { unsigned n = 0; while (x >>= 1) n++; return n; }
static double hit_rate(const dm_stats_t *s) { return s->accesses ? (double)s->hits / (double)s->accesses : 0.0; }

/* ======================= Part A — 분해와 판정 ======================= */

/* 0 = OK, -1 = 잘못된 파라미터 (C, B 가 2의 거듭제곱 아님 / B < 4 / B > C / C/B > DM_MAX_SETS).
 * 성공하면 모든 라인 무효 + 통계 0 + 본 라인 집합 비움. 실패하면 c 를 건드리지 않는다. */
static int dm_init(dmcache_t *c, uint32_t capacity, uint32_t line) {
    if (!is_pow2(capacity) || !is_pow2(line) || line < 4 || line > capacity) return -1;
    if (capacity / line > DM_MAX_SETS) return -1;
    memset(c, 0, sizeof *c);
    c->capacity = capacity;
    c->line = line;
    c->sets = capacity / line;
    c->off_bits = log2u(line);
    c->idx_bits = log2u(c->sets);
    return 0;
}

/* addr = | tag | index | offset |  (C <= 2^31 이므로 off_bits + idx_bits <= 31: 시프트 UB 없음) */
static void dm_split(const dmcache_t *c, uint32_t addr, uint32_t *tag, uint32_t *index, uint32_t *offset) {
    *offset = addr & (c->line - 1);
    *index  = (addr >> c->off_bits) & (c->sets - 1);
    *tag    = addr >> (c->off_bits + c->idx_bits);
}

/* 라인 하나 조회. 1 = hit, 0 = miss (라인을 채움), -1 = rw 가 'R'/'W' 가 아님 (통계에 안 넣는다).
 * 미스면: 처음 보는 라인 주소(addr >> off_bits)면 compulsory++, 유효 라인을 밀어내면 evictions++ 와
 * set_evictions[index]++. */
static int dm_access(dmcache_t *c, uint32_t addr, char rw) {
    if (rw != 'R' && rw != 'W') return -1;
    uint32_t tag, idx, off;
    dm_split(c, addr, &tag, &idx, &off);
    c->st.accesses++;
    if (rw == 'R') c->st.reads++; else c->st.writes++;
    if (c->valid[idx] && c->tag[idx] == tag) { c->st.hits++; return 1; }
    c->st.misses++;
    if (ls_insert(&c->seen, addr >> c->off_bits) == 1) c->st.compulsory++;
    if (c->valid[idx]) { c->st.evictions++; c->set_evictions[idx]++; }
    c->valid[idx] = 1;
    c->tag[idx] = tag;
    return 0;
}

/* 트레이스 전체 (레코드가 라인을 넘지 않는다고 가정 — 넘는 경우는 Part B dm_access_bytes) */
static void dm_run(dmcache_t *c, const trace_rec_t *t, size_t n) {
    for (size_t i = 0; i < n; i++) (void)dm_access(c, t[i].addr, t[i].rw);
}

/* ======================= Part B — 트레이스와 conflict miss ======================= */

/* size 바이트 접근을 라인 단위 조회로 쪼갠다: 라인 경계를 넘으면 2번(이상) 조회, 조회 1번 = accesses 1.
 * 반환 = 이 접근에서 난 미스 수. size 0, rw 오류, 주소 공간 끝을 넘어 감(wrap) → -1 (아무것도 안 함). */
static int dm_access_bytes(dmcache_t *c, uint32_t addr, uint32_t size, char rw) {
    if (size == 0 || (rw != 'R' && rw != 'W')) return -1;
    uint64_t end = (uint64_t)addr + size - 1;
    if (end > UINT32_MAX) return -1;
    uint32_t first = addr >> c->off_bits, last = (uint32_t)end >> c->off_bits;
    int misses = 0;
    for (uint32_t l = first;; l++) {
        if (dm_access(c, l << c->off_bits, rw) == 0) misses++;
        if (l == last) break;
    }
    return misses;
}

/* stride 반복 루프: base 부터 stride 간격 count 개를 passes 번. 반환 = 레코드 수 (max 넘으면 0). */
static size_t gen_stride(trace_rec_t *t, size_t max, uint32_t base, uint32_t count, uint32_t stride,
                         uint32_t passes, uint8_t size) {
    if ((size_t)count * passes > max) return 0;
    size_t n = 0;
    for (uint32_t p = 0; p < passes; p++)
        for (uint32_t k = 0; k < count; k++)
            t[n++] = (trace_rec_t){base + k * stride, 'R', size};
    return n;
}

/* 행렬 rows×cols, 원소 elem 바이트, 행 간격 pitch 바이트 (pitch > cols*elem 이면 행 끝에 패딩).
 * 원소 (i, j) 의 주소 = base + i*pitch + j*elem.  by_col = 0: 행 우선 (i 바깥, j 안쪽),
 * by_col = 1: 열 우선 (j 바깥, i 안쪽). 모두 'R', size = elem. 반환 = 레코드 수
 * (rows*cols > max 이거나 pitch < cols*elem 이면 0). */
static size_t gen_matrix(trace_rec_t *t, size_t max, uint32_t base, uint32_t rows, uint32_t cols,
                         uint32_t elem, uint32_t pitch, int by_col) {
    if ((size_t)rows * cols > max || pitch < cols * elem) return 0;
    size_t n = 0;
    uint32_t outer = by_col ? cols : rows, inner = by_col ? rows : cols;
    for (uint32_t o = 0; o < outer; o++)
        for (uint32_t in = 0; in < inner; in++) {
            uint32_t i = by_col ? in : o, j = by_col ? o : in;
            t[n++] = (trace_rec_t){base + i * pitch + j * elem, 'R', (uint8_t)elem};
        }
    return n;
}

/* 시뮬레이션 없이 주소만 보고: 트레이스의 레코드들이 매핑되는 서로 다른 set 의 수. */
static uint32_t dm_sets_touched(const dmcache_t *c, const trace_rec_t *t, size_t n) {
    static uint8_t mark[DM_MAX_SETS];
    memset(mark, 0, sizeof mark);
    uint32_t k = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t idx = (t[i].addr >> c->off_bits) & (c->sets - 1);
        if (!mark[idx]) { mark[idx] = 1; k++; }
    }
    return k;
}

/* 지금까지 본 서로 다른 라인 수(= compulsory) 가 캐시 라인 수(S) 이하면, 같은 용량의 완전 연관 LRU 캐시는
 * 한 번도 내보낼 일이 없다 → compulsory 가 아닌 미스는 전부 conflict miss. 그 수를 돌려준다.
 * 워킹셋이 S 보다 크면 capacity 와 conflict 를 여기서는 가를 수 없다 → UINT64_MAX (정확한 3C 는 ARCH-M05). */
static uint64_t dm_conflict_if_fits(const dmcache_t *c) {
    if (c->st.compulsory > c->sets) return UINT64_MAX;
    return c->st.misses - c->st.compulsory;
}

/* ======================= Part C — 플래시 명령 캐시와 ISR 배치 ======================= */

/* 바이트 범위 [a, a+la), [b, b+lb) 가 각각 덮는 set 집합의 교집합 크기. la == 0 또는 lb == 0 → 0.
 * 범위가 C 이상이면 모든 set 을 덮는다. (범위는 주소 공간 끝을 넘지 않는다고 가정) */
static uint32_t ranges_shared_sets(const dmcache_t *c, uint32_t a, uint32_t la, uint32_t b, uint32_t lb) {
    if (la == 0 || lb == 0) return 0;
    static uint8_t in_a[DM_MAX_SETS];
    memset(in_a, 0, sizeof in_a);
    uint32_t a0 = a >> c->off_bits, a1 = (a + la - 1) >> c->off_bits;
    uint32_t b0 = b >> c->off_bits, b1 = (b + lb - 1) >> c->off_bits;
    for (uint32_t l = a0; l - a0 < c->sets && l <= a1; l++) in_a[l & (c->sets - 1)] = 1;
    uint32_t shared = 0;
    for (uint32_t l = b0; l - b0 < c->sets && l <= b1; l++)
        if (in_a[l & (c->sets - 1)]) { in_a[l & (c->sets - 1)] = 0; shared++; }
    return shared;
}

/* hot 범위 [hot, hot+hot_len) 와 set 을 하나도 공유하지 않는 len 바이트 코드의 시작 주소 중
 * start 이상이고 라인 정렬된 가장 작은 주소를 *out 에. 0 = 찾음, -1 = 불가능 (len + hot 이 덮는 라인 > S 등).
 * (C 바이트만 훑어보면 충분하다: 주소를 C 만큼 옮기면 set 매핑이 똑같이 반복된다) */
static int place_no_conflict(const dmcache_t *c, uint32_t hot, uint32_t hot_len, uint32_t start,
                             uint32_t len, uint32_t *out) {
    if (len == 0 || hot_len == 0) return -1;
    uint32_t a = (start + c->line - 1) & ~(c->line - 1);
    for (uint32_t k = 0; k < c->sets; k++, a += c->line)
        if (ranges_shared_sets(c, hot, hot_len, a, len) == 0) { *out = a; return 0; }
    return -1;
}

/* 펌웨어 fetch 트레이스 (주어진 도구): 메인 루프 loop_words 개 4 B 명령을 iters 번 돌고,
 * 매 isr_every 번째 반복이 끝날 때마다 ISR 의 isr_words 개 명령을 실행한다. */
static size_t gen_fw_fetch(trace_rec_t *t, size_t max, uint32_t loop_base, uint32_t loop_words, uint32_t iters,
                           uint32_t isr_base, uint32_t isr_words, uint32_t isr_every) {
    size_t n = 0;
    for (uint32_t it = 1; it <= iters; it++) {
        for (uint32_t w = 0; w < loop_words && n < max; w++) t[n++] = (trace_rec_t){loop_base + 4 * w, 'R', 4};
        if (isr_every && it % isr_every == 0)
            for (uint32_t w = 0; w < isr_words && n < max; w++) t[n++] = (trace_rec_t){isr_base + 4 * w, 'R', 4};
    }
    return n;
}

/* fetch cycle 추정: 히트 1 cycle, 미스는 penalty cycle 추가 (예: 플래시 wait state 로 라인 한 번 읽기) */
static uint64_t fetch_cycles(const dm_stats_t *s, uint32_t penalty) { return s->accesses + s->misses * penalty; }

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static uint32_t xs_state = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}

/* 참조 모델: 시프트/마스크 대신 나눗셈/나머지로, set 마다 "들어 있는 라인 번호" 하나를 기억 */
static struct { uint32_t B, S; uint8_t valid[DM_MAX_SETS]; uint32_t line_no[DM_MAX_SETS]; } R;
static void ref_init(uint32_t C, uint32_t B) { memset(&R, 0, sizeof R); R.B = B; R.S = C / B; }
static int ref_access(uint32_t addr) {
    uint32_t ln = addr / R.B, set = ln % R.S;
    if (R.valid[set] && R.line_no[set] == ln) return 1;
    R.valid[set] = 1; R.line_no[set] = ln;
    return 0;
}

static dmcache_t g_c;                 /* 큰 구조체 → 정적 */
static trace_rec_t g_t[16384];

static dm_stats_t run_matrix(uint32_t C, uint32_t B, uint32_t rows, uint32_t cols, uint32_t pitch, int by_col) {
    dm_init(&g_c, C, B);
    size_t n = gen_matrix(g_t, 16384, 0x20000000u, rows, cols, 4, pitch, by_col);
    dm_run(&g_c, g_t, n);
    return g_c.st;
}

int main(void) {
    uint32_t tag, idx, off;

    /* ---------- Part A ---------- */
    printf("-- Part A: geometry, split, hit/miss --\n");
    CHECK(dm_init(&g_c, 4096, 32) == 0 && g_c.sets == 128 && g_c.off_bits == 5 && g_c.idx_bits == 7);
    CHECK(dm_init(&g_c, 32, 32) == 0 && g_c.sets == 1 && g_c.idx_bits == 0);       /* 라인 1개짜리 */
    CHECK(dm_init(&g_c, 4096, 24) == -1);                                          /* B 가 2^k 아님 */
    CHECK(dm_init(&g_c, 3000, 32) == -1);                                          /* C 가 2^k 아님 */
    CHECK(dm_init(&g_c, 32, 64) == -1);                                            /* B > C */
    CHECK(dm_init(&g_c, 4096, 2) == -1);                                           /* B < 4 */
    CHECK(dm_init(&g_c, 1u << 20, 16) == -1);                                      /* 65536 set > 한도 */
    CHECK(dm_init(&g_c, 0, 32) == -1);
    dm_init(&g_c, 4096, 32);
    dm_split(&g_c, 0x12345678u, &tag, &idx, &off);
    CHECK(tag == 0x12345u && idx == 0x33u && off == 0x18u);                        /* 20 | 7 | 5 비트 */
    dm_split(&g_c, 0xFFFFFFFFu, &tag, &idx, &off);
    CHECK(tag == 0xFFFFFu && idx == 127u && off == 31u);
    {   /* 분해 → 재조립 = 원래 주소 (여러 형상 × 랜덤 주소) */
        static const uint32_t geo[][2] = {{32, 4}, {1024, 16}, {4096, 32}, {65536, 64}, {131072, 32}, {256, 256}};
        int ok = 1;
        for (unsigned g = 0; g < sizeof geo / sizeof geo[0]; g++) {
            dm_init(&g_c, geo[g][0], geo[g][1]);
            for (int k = 0; k < 2000; k++) {
                uint32_t a = xorshift32();
                dm_split(&g_c, a, &tag, &idx, &off);
                uint32_t back = (uint32_t)(((uint64_t)tag << (g_c.off_bits + g_c.idx_bits)) |
                                           ((uint64_t)idx << g_c.off_bits) | off);
                if (back != a || idx >= g_c.sets || off >= g_c.line) ok = 0;
            }
        }
        CHECK(ok);
    }
    {   /* 4 KB 순차 읽기 (4 B 단위) → 라인당 첫 접근만 미스: 7/8 히트 */
        dm_init(&g_c, 4096, 32);
        size_t n = gen_stride(g_t, 16384, 0x20000000u, 1024, 4, 1, 4);
        dm_run(&g_c, g_t, n);
        CHECK(g_c.st.accesses == 1024 && g_c.st.misses == 128 && g_c.st.compulsory == 128);
        CHECK(g_c.st.hits == 896 && hit_rate(&g_c.st) == 0.875 && g_c.st.evictions == 0);
        dm_run(&g_c, g_t, n);                                                       /* 두 번째: 딱 들어감 */
        CHECK(g_c.st.misses == 128 && g_c.st.hits == 1920);
    }
    {   /* 8 KB 순차 두 번: 앞 4 KB 가 뒤 4 KB 에 밀려 둘째 바퀴도 라인마다 미스 */
        dm_init(&g_c, 4096, 32);
        size_t n = gen_stride(g_t, 16384, 0x20000000u, 2048, 4, 2, 4);
        dm_run(&g_c, g_t, n);
        CHECK(g_c.st.misses == 512 && g_c.st.compulsory == 256 && g_c.st.evictions == 384);
        CHECK(dm_conflict_if_fits(&g_c) == UINT64_MAX);                             /* 워킹셋 8 KB > C */
    }
    {   /* rw: 'W' 도 같은 판정 (write-allocate 가정), 이상한 rw 는 거부하고 세지 않는다 */
        dm_init(&g_c, 4096, 32);
        CHECK(dm_access(&g_c, 0x100, 'W') == 0 && dm_access(&g_c, 0x104, 'R') == 1);
        CHECK(dm_access(&g_c, 0x100, 'x') == -1 && g_c.st.accesses == 2);
        CHECK(g_c.st.reads == 1 && g_c.st.writes == 1);
        CHECK(dm_access(&g_c, 0x1100, 'R') == 0 && g_c.st.evictions == 1 && g_c.set_evictions[8] == 1);
        CHECK(dm_access(&g_c, 0x100, 'R') == 0 && g_c.st.compulsory == 2);         /* 다시 오면 conflict */
    }
    {   /* 결정적 퍼즈: 30개 형상 × 4000 접근, 참조 모델과 hit/miss 가 접근마다 같아야 한다 */
        int mism = 0, cfgs = 0;
        uint64_t total_hits = 0;
        for (int cfg = 0; cfg < 30; cfg++) {
            uint32_t B = 4u << (xorshift32() % 7);                 /* 4..256 */
            uint32_t C = B << (xorshift32() % 13);                 /* B..4096·B */
            if (C / B > DM_MAX_SETS || dm_init(&g_c, C, B) != 0) continue;
            cfgs++;
            ref_init(C, B);
            uint32_t a = xorshift32() & ~3u;
            for (int k = 0; k < 4000; k++) {
                uint32_t r = xorshift32() % 8;
                if (r < 5) a += 4;                                 /* 순차 */
                else if (r < 7) a = (a & ~(4 * C - 1)) | (xorshift32() & (4 * C - 1) & ~3u); /* 근처 */
                else a = xorshift32() & ~3u;                       /* 아무 데나 */
                int h = dm_access(&g_c, a, (k & 1) ? 'W' : 'R');
                if (h != ref_access(a)) mism++;
            }
            total_hits += g_c.st.hits;
            if (g_c.st.hits + g_c.st.misses != g_c.st.accesses) mism++;
        }
        CHECK(cfgs >= 20 && mism == 0);
        CHECK(total_hits > 0);
    }

    /* ---------- Part B ---------- */
    printf("-- Part B: straddle, traces, conflict misses --\n");
    dm_init(&g_c, 4096, 32);
    CHECK(dm_access_bytes(&g_c, 0x1E, 4, 'R') == 2 && g_c.st.accesses == 2);       /* 0x1E..0x21: 두 라인 */
    CHECK(dm_access_bytes(&g_c, 0x1C, 4, 'R') == 0);                               /* 0x1C..0x1F: 라인 0 히트 */
    CHECK(dm_access_bytes(&g_c, 0x40, 64, 'W') == 2 && g_c.st.accesses == 5);
    CHECK(dm_access_bytes(&g_c, 0x40, 0, 'R') == -1);
    CHECK(dm_access_bytes(&g_c, 0xFFFFFFFEu, 4, 'R') == -1 && g_c.st.accesses == 5); /* wrap 거부 */
    CHECK(dm_access_bytes(&g_c, 0xFFFFFFFCu, 4, 'R') == 1);                        /* 끝 라인 딱 맞음 */

    CHECK(gen_matrix(g_t, 16384, 0, 64, 64, 4, 256, 0) == 4096);
    CHECK(g_t[1].addr == 4 && g_t[64].addr == 256 && g_t[4095].addr == 63 * 256 + 63 * 4);
    CHECK(gen_matrix(g_t, 16384, 0, 64, 64, 4, 256, 1) == 4096);
    CHECK(g_t[1].addr == 256 && g_t[64].addr == 4 && g_t[4095].addr == 63 * 256 + 63 * 4);
    CHECK(gen_matrix(g_t, 16384, 0, 64, 64, 4, 252, 0) == 0);                      /* pitch < 행 크기 */
    CHECK(gen_matrix(g_t, 100, 0, 64, 64, 4, 256, 0) == 0);                        /* 버퍼 부족 */

    {   /* 64×64 int 행렬 (16 KB) 를 4 KB/32 B 캐시로: 행 우선 vs 열 우선 vs 열 우선 + 한 라인 패딩 */
        dm_stats_t row = run_matrix(4096, 32, 64, 64, 256, 0);
        dm_stats_t col = run_matrix(4096, 32, 64, 64, 256, 1);
        dm_stats_t colp = run_matrix(4096, 32, 64, 64, 288, 1);
        CHECK(row.misses == 512 && hit_rate(&row) == 0.875);
        CHECK(col.hits == 0 && col.misses == 4096);                                /* 전부 미스 */
        CHECK(colp.misses == 512 && hit_rate(&colp) == 0.875);                     /* 패딩 32 B 로 회복 */
        dm_init(&g_c, 4096, 32);
        gen_matrix(g_t, 16384, 0, 64, 64, 4, 256, 1);
        CHECK(dm_sets_touched(&g_c, g_t, 64) == 16);                               /* 한 열 64줄 → 16 set */
        gen_matrix(g_t, 16384, 0, 64, 64, 4, 288, 1);
        CHECK(dm_sets_touched(&g_c, g_t, 64) == 64);                               /* 9 와 128 은 서로소 */
    }
    {   /* 순수 conflict: 1 KB pitch 이미지 안의 16×16 int 타일 (워킹셋 32 라인 ≤ 128) */
        dm_stats_t col = run_matrix(4096, 32, 16, 16, 1024, 1);
        CHECK(col.compulsory == 32 && col.misses == 256 && dm_conflict_if_fits(&g_c) == 224);
        dm_stats_t colp = run_matrix(4096, 32, 16, 16, 1056, 1);
        CHECK(colp.misses == 32 && dm_conflict_if_fits(&g_c) == 0);
    }
    {   /* 내적: a[i]*b[i], b 가 a 에서 정확히 C 떨어짐 → 같은 set 을 번갈아 쫓아냄 */
        dm_init(&g_c, 4096, 32);
        for (uint32_t i = 0; i < 256; i++) {
            dm_access(&g_c, 0x20000000u + 4 * i, 'R');
            dm_access(&g_c, 0x20001000u + 4 * i, 'R');
        }
        CHECK(g_c.st.misses == 512 && g_c.st.compulsory == 64 && dm_conflict_if_fits(&g_c) == 448);
        dm_init(&g_c, 4096, 32);
        for (uint32_t i = 0; i < 256; i++) {
            dm_access(&g_c, 0x20000000u + 4 * i, 'R');
            dm_access(&g_c, 0x20001020u + 4 * i, 'R');                             /* + 한 라인 패딩 */
        }
        CHECK(g_c.st.misses == 64 && dm_conflict_if_fits(&g_c) == 0);
    }
    {   /* stride 반복 루프: 64개 원소 × 4바퀴. stride 가 C/32 이상이면 set 이 모자라 thrash */
        static const uint32_t strides[] = {4, 32, 64, 128, 4096};
        static const uint64_t want[]    = {8, 64, 64, 256, 256};
        static const uint32_t sets[]    = {8, 64, 64, 32, 1};
        int ok = 1;
        for (int s = 0; s < 5; s++) {
            dm_init(&g_c, 4096, 32);
            size_t n = gen_stride(g_t, 16384, 0x20000000u, 64, strides[s], 4, 4);
            dm_run(&g_c, g_t, n);
            if (g_c.st.misses != want[s] || dm_sets_touched(&g_c, g_t, 64) != sets[s]) ok = 0;
        }
        CHECK(ok);
    }

    /* ---------- Part C ---------- */
    printf("-- Part C: flash I-cache, main loop vs ISR --\n");
    dm_init(&g_c, 1024, 16);                                                        /* 64 라인 × 128 bit */
    CHECK(ranges_shared_sets(&g_c, 0x08000400u, 256, 0x08001400u, 128) == 8);      /* 4 KB 간격 = C 의 배수 */
    CHECK(ranges_shared_sets(&g_c, 0x08000400u, 256, 0x08001500u, 128) == 0);
    CHECK(ranges_shared_sets(&g_c, 0x08000408u, 16, 0x08001400u, 128) == 2);       /* 걸친 라인도 센다 */
    CHECK(ranges_shared_sets(&g_c, 0x08000000u, 4096, 0x08009000u, 64) == 4);      /* 범위 ≥ C: 모든 set */
    CHECK(ranges_shared_sets(&g_c, 0x08000000u, 0, 0x08000000u, 64) == 0);
    {
        uint32_t at = 0;
        CHECK(place_no_conflict(&g_c, 0x08000400u, 256, 0x08001400u, 128, &at) == 0 && at == 0x08001500u);
        CHECK(place_no_conflict(&g_c, 0x08000400u, 256, 0x08001401u, 128, &at) == 0 && at == 0x08001500u);
        CHECK(place_no_conflict(&g_c, 0x08000400u, 1024, 0x08001400u, 16, &at) == -1); /* 루프가 캐시 전체 */
        CHECK(place_no_conflict(&g_c, 0x08000400u, 768, 0x08001400u, 512, &at) == -1); /* 합쳐서 C 초과 */
        CHECK(place_no_conflict(&g_c, 0x08000000u, 512, 0x08000F00u, 512, &at) == 0 && at == 0x08001200u);
    }
    {   /* 루프 64 명령(256 B) × 100 바퀴, 10 바퀴마다 ISR 32 명령(128 B). 미스 1번 = +5 cycle (예시) */
        size_t n = gen_fw_fetch(g_t, 16384, 0x08000400u, 64, 100, 0x08001400u, 32, 10);
        CHECK(n == 6720);
        dm_init(&g_c, 1024, 16);
        dm_run(&g_c, g_t, n);
        dm_stats_t bad = g_c.st;
        CHECK(bad.misses == 168 && bad.compulsory == 24);                          /* 16 + 10·8 + 9·8 */
        CHECK(dm_conflict_if_fits(&g_c) == 144);
        n = gen_fw_fetch(g_t, 16384, 0x08000400u, 64, 100, 0x08001500u, 32, 10);
        dm_init(&g_c, 1024, 16);
        dm_run(&g_c, g_t, n);
        CHECK(g_c.st.misses == 24 && dm_conflict_if_fits(&g_c) == 0);              /* 첫 접근만 */
        CHECK(fetch_cycles(&bad, 5) == 7560 && fetch_cycles(&g_c.st, 5) == 6840);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
