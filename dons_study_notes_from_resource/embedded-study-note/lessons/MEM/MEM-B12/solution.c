/* MEM-B12 NAND 플래시 기초: 페이지/블록, program/erase 제약
 *
 * FTL 없는 NAND "칩 + 규칙 검사기" 모델. MEM-M10..M14 (FTL) 가 이 API 를 그대로 품는다.
 *   Part A: 지오메트리(plane/block/page + OOB), 셀 타입 표(SLC/MLC/TLC/QLC 대표값),
 *           nand_read / nand_program / nand_erase. erased = 0xFF, program 은 비트를 1→0 으로만 바꾼다 (AND).
 *   Part B: 규칙 — ERR_NOT_ERASED(재프로그램 · NOP 초과), ERR_OUT_OF_ORDER(블록 안 순서), ERR_BAD_BLOCK,
 *           ERR_WORN_OUT(P/E ≥ 내구도 → erase/program status FAIL), ERR_RANGE. plane 별 바쁜 시간(µs) 누적.
 *   Part C: 드라이버 계약 — 공장 불량 블록 스캔(BBT), in-place 갱신(실패 / RMW 비용) vs out-of-place 로그.
 *
 * 수치(내구도, tR/tPROG/tBERS, NOP, 순서 규칙)는 모두 대표값 — 실제 부품은 데이터시트 확인.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 공통 정의 (주어짐) ---------------- */
typedef enum {
    NAND_OK = 0,
    NAND_ERR_RANGE,         /* blk/page 범위 밖 (Part C 로그에서는 "빈 페이지 없음") */
    NAND_ERR_PARAM,         /* NULL 포인터, 잘못된 지오메트리 */
    NAND_ERR_NOT_ERASED,    /* 이미 데이터가 있는 페이지 (재프로그램, NOP 초과) */
    NAND_ERR_OUT_OF_ORDER,  /* 지워져 있지만 블록 안 순서상 지금 쓸 수 없는 페이지 */
    NAND_ERR_BAD_BLOCK,     /* 공장 불량 블록에 program/erase */
    NAND_ERR_WORN_OUT,      /* P/E 수명 끝 → status FAIL */
} nand_rc;
static const char *RC_NAME[] = { "OK", "ERR_RANGE", "ERR_PARAM", "ERR_NOT_ERASED",
                                 "ERR_OUT_OF_ORDER", "ERR_BAD_BLOCK", "ERR_WORN_OUT" };

typedef enum { CELL_SLC, CELL_MLC, CELL_TLC, CELL_QLC, CELL_COUNT } cell_t;
typedef struct {
    const char *name;
    unsigned bits;           /* 셀당 비트 */
    uint32_t endurance;      /* 정격 P/E 사이클 */
    uint32_t t_r_us, t_prog_us, t_bers_us;   /* 페이지 읽기 / 페이지 프로그램 / 블록 지우기 (typ) */
    uint8_t  nop;            /* erase 1회당 같은 페이지에 허용되는 program 횟수 (partial page program) */
    bool     strict_seq;     /* true: page == next 만 허용. false: page >= next (앞으로 건너뛰기 허용) */
} cell_param;

/* 대표값 (데이터시트·논문에 흔한 범위의 가운데) — 세대/벤더마다 2배 이상 다르다. */
static const cell_param CELL[CELL_COUNT] = {
    /* name  bits endurance   tR  tPROG  tBERS  NOP strict */
    { "SLC", 1,  100000,      25,   200,  2000,  4, false },
    { "MLC", 2,    3000,      50,   800,  3000,  1, true  },
    { "TLC", 3,    1500,      70,  1000,  3500,  1, true  },
    { "QLC", 4,     500,     140,  2500,  8000,  1, true  },
};

enum { NAND_MAX_PLANES = 4, NAND_MAX_BLOCKS = 64, NAND_MAX_PAGES = 64,
       NAND_MAX_PAGE_BYTES = 4096, NAND_MAX_OOB = 256,
       NAND_MAX_STORE = 64 * 1024 };   /* 데이터+OOB 전체 바이트 상한 (정적 버퍼) */

typedef struct {
    uint32_t planes, blocks_per_plane, pages_per_block, page_bytes, oob_bytes;
    cell_t   cell;
} nand_geo;

typedef enum { BLK_GOOD = 0, BLK_FACTORY_BAD, BLK_WORN } blk_state;

typedef struct {
    nand_geo   g;
    cell_param p;
    uint32_t   nblocks;
    uint8_t    store[NAND_MAX_STORE];                        /* [blk][page] = data(page_bytes) ‖ oob(oob_bytes) */
    uint8_t    prog_cnt[NAND_MAX_BLOCKS * NAND_MAX_PAGES];   /* 마지막 erase 이후 이 페이지를 program 한 횟수 */
    uint16_t   next_page[NAND_MAX_BLOCKS];   /* 블록 쓰기 포인터 = 마지막으로 program 한 페이지 + 1 */
    uint32_t   pe[NAND_MAX_BLOCKS];          /* 성공한 erase 횟수 (P/E 카운트) */
    uint8_t    state[NAND_MAX_BLOCKS];       /* blk_state — "칩 내부 진실". 드라이버는 Part C 처럼 읽어서 알아낸다 */
    uint64_t   busy_us[NAND_MAX_PLANES];     /* plane 별 누적 바쁜 시간 (status FAIL 난 시도 포함) */
    uint32_t   n_read, n_prog, n_erase;      /* 성공한 명령 수 */
} nand_t;

static uint32_t plane_of(const nand_t *n, uint32_t blk) { return blk % n->g.planes; }  /* 블록 주소 하위 = plane */
static size_t   slot_bytes(const nand_t *n) { return (size_t)n->g.page_bytes + n->g.oob_bytes; }
static size_t   pidx(const nand_t *n, uint32_t blk, uint32_t page) { return (size_t)blk * n->g.pages_per_block + page; }
static uint8_t *page_ptr(nand_t *n, uint32_t blk, uint32_t page) { return n->store + pidx(n, blk, page) * slot_bytes(n); }
static bool     in_range(const nand_t *n, uint32_t blk, uint32_t page) {
    return blk < n->nblocks && page < n->g.pages_per_block;
}

/* 지오메트리 검사 + 전부 erased(0xFF) 상태로 "출하". factory_bad[] 블록은 page 0 OOB byte 0 = 0x00 으로 표시
 * (ONFI/벤더 관례를 단순화 — 실제 마커 위치는 데이터시트 확인). */
static nand_rc nand_init(nand_t *n, const nand_geo *g, const uint32_t *factory_bad, unsigned nbad) {
    if (!n || !g) return NAND_ERR_PARAM;
    if (g->planes == 0 || g->planes > NAND_MAX_PLANES || g->blocks_per_plane == 0 ||
        g->pages_per_block == 0 || g->pages_per_block > NAND_MAX_PAGES ||
        g->page_bytes == 0 || g->page_bytes > NAND_MAX_PAGE_BYTES ||
        g->oob_bytes == 0 || g->oob_bytes > NAND_MAX_OOB || (unsigned)g->cell >= CELL_COUNT)
        return NAND_ERR_PARAM;
    uint64_t nb = (uint64_t)g->planes * g->blocks_per_plane;
    if (nb > NAND_MAX_BLOCKS) return NAND_ERR_PARAM;
    if (nb * g->pages_per_block * ((uint64_t)g->page_bytes + g->oob_bytes) > NAND_MAX_STORE) return NAND_ERR_PARAM;
    for (unsigned i = 0; i < nbad; i++)
        if (!factory_bad || factory_bad[i] >= nb) return NAND_ERR_PARAM;
    memset(n, 0, sizeof *n);
    n->g = *g;
    n->p = CELL[g->cell];
    n->nblocks = (uint32_t)nb;
    memset(n->store, 0xFF, (size_t)nb * g->pages_per_block * slot_bytes(n));
    for (unsigned i = 0; i < nbad; i++) {
        n->state[factory_bad[i]] = BLK_FACTORY_BAD;
        page_ptr(n, factory_bad[i], 0)[g->page_bytes] = 0x00;     /* OOB byte 0 of page 0 */
    }
    return NAND_OK;
}

static uint64_t busy_total(const nand_t *n) { uint64_t s = 0; for (uint32_t i = 0; i < n->g.planes; i++) s += n->busy_us[i]; return s; }
static uint64_t makespan(const nand_t *n) {   /* plane 들이 완전히 겹쳐 돈다고 볼 때 걸리는 시간 */
    uint64_t m = 0; for (uint32_t i = 0; i < n->g.planes; i++) if (n->busy_us[i] > m) m = n->busy_us[i]; return m;
}

/* ---------------- Part A: read / program / erase ---------------- */

/* 페이지 읽기. data / oob 중 NULL 인 쪽은 건너뛴다 (OOB 만 읽기 = 불량 마커 스캔).
 * 불량·마모 블록도 읽을 수 있다 (마커를 읽어야 하니까). 성공 시 tR 을 그 plane 에 더한다. */
static nand_rc nand_read(nand_t *n, uint32_t blk, uint32_t page, uint8_t *data, uint8_t *oob) {
    if (!n) return NAND_ERR_PARAM;
    if (!in_range(n, blk, page)) return NAND_ERR_RANGE;
    const uint8_t *s = page_ptr(n, blk, page);
    if (data) memcpy(data, s, n->g.page_bytes);
    if (oob)  memcpy(oob, s + n->g.page_bytes, n->g.oob_bytes);
    n->busy_us[plane_of(n, blk)] += n->p.t_r_us;
    n->n_read++;
    return NAND_OK;
}

/* ---------------- Part B: 규칙 검사 (우선순위 표는 md 5장) ---------------- */

/* blk/page 는 범위 안이라고 가정. 아래 순서대로 판정한다:
 *   1) 공장 불량 → BAD_BLOCK   2) 마모 → WORN_OUT
 *   3) 이미 program 된 페이지: "마지막으로 쓴 페이지" 이고 횟수 < NOP 이면 OK (partial), 아니면 NOT_ERASED
 *   4) 지워진 페이지인데 쓰기 포인터보다 뒤 → OUT_OF_ORDER (건너뛴 페이지로 되돌아감)
 *   5) strict_seq 인데 쓰기 포인터보다 앞 → OUT_OF_ORDER (앞으로 건너뛰기 금지)            */
static nand_rc program_check(const nand_t *n, uint32_t blk, uint32_t page) {
    if (n->state[blk] == BLK_FACTORY_BAD) return NAND_ERR_BAD_BLOCK;
    if (n->state[blk] == BLK_WORN) return NAND_ERR_WORN_OUT;
    uint32_t next = n->next_page[blk];
    uint8_t cnt = n->prog_cnt[pidx(n, blk, page)];
    if (cnt > 0) {
        if (page + 1 == next && cnt < n->p.nop) return NAND_OK;      /* 마지막 페이지에 partial program */
        return NAND_ERR_NOT_ERASED;
    }
    if (page < next) return NAND_ERR_OUT_OF_ORDER;
    if (n->p.strict_seq && page != next) return NAND_ERR_OUT_OF_ORDER;
    return NAND_OK;
}

/* 페이지 프로그램. 셀은 1→0 만 가능하므로 저장값 &= 입력값. oob == NULL 이면 OOB 는 그대로(0xFF 를 쓴 것과 같음).
 * 규칙 위반은 칩에 보내지 않으므로 시간 0. 마모(WORN_OUT)는 칩이 시도했다가 FAIL → tPROG 를 더한다. */
static nand_rc nand_program(nand_t *n, uint32_t blk, uint32_t page, const uint8_t *data, const uint8_t *oob) {
    if (!n || !data) return NAND_ERR_PARAM;
    if (!in_range(n, blk, page)) return NAND_ERR_RANGE;
    nand_rc rc = program_check(n, blk, page);
    if (rc == NAND_ERR_WORN_OUT) n->busy_us[plane_of(n, blk)] += n->p.t_prog_us;
    if (rc != NAND_OK) return rc;
    uint8_t *s = page_ptr(n, blk, page);
    for (uint32_t i = 0; i < n->g.page_bytes; i++) s[i] &= data[i];
    if (oob) for (uint32_t i = 0; i < n->g.oob_bytes; i++) s[n->g.page_bytes + i] &= oob[i];
    n->prog_cnt[pidx(n, blk, page)]++;
    if (page >= n->next_page[blk]) n->next_page[blk] = (uint16_t)(page + 1);
    n->busy_us[plane_of(n, blk)] += n->p.t_prog_us;
    n->n_prog++;
    return NAND_OK;
}

/* 블록 지우기: 데이터+OOB 전부 0xFF, 쓰기 포인터·program 횟수 리셋, P/E +1.
 * 공장 불량 → BAD_BLOCK (지우면 마커가 영영 사라진다, 시간 0).
 * pe ≥ endurance (또는 이미 WORN) → 블록을 WORN(grown bad)으로 바꾸고 tBERS 를 쓴 뒤 WORN_OUT. 내용은 그대로 둔다. */
static nand_rc nand_erase(nand_t *n, uint32_t blk) {
    if (!n) return NAND_ERR_PARAM;
    if (blk >= n->nblocks) return NAND_ERR_RANGE;
    if (n->state[blk] == BLK_FACTORY_BAD) return NAND_ERR_BAD_BLOCK;
    uint32_t pl = plane_of(n, blk);
    if (n->state[blk] == BLK_WORN || n->pe[blk] >= n->p.endurance) {
        n->state[blk] = BLK_WORN;
        n->busy_us[pl] += n->p.t_bers_us;
        return NAND_ERR_WORN_OUT;
    }
    memset(page_ptr(n, blk, 0), 0xFF, (size_t)n->g.pages_per_block * slot_bytes(n));
    memset(&n->prog_cnt[pidx(n, blk, 0)], 0, n->g.pages_per_block);
    n->next_page[blk] = 0;
    n->pe[blk]++;
    n->busy_us[pl] += n->p.t_bers_us;
    n->n_erase++;
    return NAND_OK;
}

/* ---------------- Part C: 드라이버 계약 ---------------- */

/* 공장 불량 스캔: state[] 를 훔쳐보지 말고 nand_read 로 page 0 의 OOB byte 0 만 읽어 판정 (≠ 0xFF → 불량).
 * bbt[blk] = 1(불량) / 0. 반환: 불량 블록 수, NULL·cap 부족이면 -1. 처음 erase 하기 "전에" 돌려야 한다. */
static int nand_scan_bbt(nand_t *n, uint8_t *bbt, unsigned cap) {
    if (!n || !bbt || cap < n->nblocks) return -1;
    uint8_t oob[NAND_MAX_OOB];
    int bad = 0;
    for (uint32_t b = 0; b < n->nblocks; b++) {
        if (nand_read(n, b, 0, NULL, oob) != NAND_OK) return -1;
        bbt[b] = oob[0] != 0xFF;
        bad += bbt[b];
    }
    return bad;
}

/* 방법 1 — in-place: 블록 blk 의 page i = LBA i 로 고정 매핑 (블록은 이미 꽉 차 있다고 가정).
 * 갱신 = 블록 전체 read → 버퍼에서 한 페이지 교체 → erase → 전부 다시 program (read-modify-erase-write).
 * 첫 오류를 그대로 돌려준다. (그냥 nand_program 으로 덮어쓰면 ERR_NOT_ERASED.) */
static nand_rc naive_update(nand_t *n, uint32_t blk, uint32_t lba, const uint8_t *data) {
    static uint8_t buf[NAND_MAX_PAGES][NAND_MAX_PAGE_BYTES];
    if (!n || !data) return NAND_ERR_PARAM;
    if (blk >= n->nblocks || lba >= n->g.pages_per_block) return NAND_ERR_RANGE;
    nand_rc rc;
    for (uint32_t p = 0; p < n->g.pages_per_block; p++)
        if ((rc = nand_read(n, blk, p, buf[p], NULL)) != NAND_OK) return rc;
    memcpy(buf[lba], data, n->g.page_bytes);
    if ((rc = nand_erase(n, blk)) != NAND_OK) return rc;
    for (uint32_t p = 0; p < n->g.pages_per_block; p++)
        if ((rc = nand_program(n, blk, p, buf[p], NULL)) != NAND_OK) return rc;
    return NAND_OK;
}

/* 방법 2 — out-of-place 로그 (FTL 의 씨앗, GC 없음). */
#define NONE UINT32_MAX
typedef struct {
    uint32_t map_blk[NAND_MAX_PAGES], map_page[NAND_MAX_PAGES];   /* LBA → 최신 위치 (NONE = 아직 없음) */
    uint32_t nlba;
    uint32_t active, wp;          /* 활성 블록, 그 블록에서 다음에 쓸 페이지 */
    uint32_t stale;               /* 덮어써져 쓸모없어진(invalid) 페이지 수 — GC 가 회수할 몫 (MEM-M11) */
    const uint8_t *bbt;           /* nand_scan_bbt 결과 — 불량 블록은 건너뛴다 */
} log_t;

static void log_init(log_t *L, uint32_t nlba, const uint8_t *bbt) {
    memset(L, 0, sizeof *L);
    for (uint32_t i = 0; i < NAND_MAX_PAGES; i++) L->map_blk[i] = L->map_page[i] = NONE;
    L->nlba = nlba;
    L->bbt = bbt;
}

/* 항상 "다음 빈 페이지"에 쓰고 맵만 바꾼다. 옛 페이지는 stale 로 남는다 (erase 0회).
 * 활성 블록이 차면 다음 번호의 양품 블록으로. 더 없으면 NAND_ERR_RANGE (= GC 가 필요, MEM-M11).
 * OOB: byte 0 은 불량 마커 자리라 0xFF 로 비워 두고, byte 1..4 에 LBA(LE) 를 적는다 (P2L — MEM-M10/M14 복구용). */
static nand_rc log_write(nand_t *n, log_t *L, uint32_t lba, const uint8_t *data) {
    if (!n || !L || !data) return NAND_ERR_PARAM;
    if (lba >= L->nlba) return NAND_ERR_RANGE;
    for (;;) {
        if (L->active >= n->nblocks) return NAND_ERR_RANGE;
        if (L->wp < n->g.pages_per_block && !L->bbt[L->active]) break;
        L->active++;
        L->wp = 0;
    }
    uint8_t oob[NAND_MAX_OOB];
    memset(oob, 0xFF, sizeof oob);
    for (uint32_t k = 0; k < 4 && 1 + k < n->g.oob_bytes; k++) oob[1 + k] = (uint8_t)(lba >> (8 * k));
    nand_rc rc = nand_program(n, L->active, L->wp, data, oob);
    if (rc != NAND_OK) return rc;
    if (L->map_blk[lba] != NONE) L->stale++;
    L->map_blk[lba] = L->active;
    L->map_page[lba] = L->wp;
    L->wp++;
    return NAND_OK;
}

/* 맵을 따라 최신 사본을 읽는다. 한 번도 안 쓴 LBA 는 erased 처럼 0xFF 로 채운다. */
static nand_rc log_read(nand_t *n, const log_t *L, uint32_t lba, uint8_t *data) {
    if (!n || !L || !data) return NAND_ERR_PARAM;
    if (lba >= L->nlba) return NAND_ERR_RANGE;
    if (L->map_blk[lba] == NONE) { memset(data, 0xFF, n->g.page_bytes); return NAND_OK; }
    return nand_read(n, L->map_blk[lba], L->map_page[lba], data, NULL);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x12B12u;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static void fill_rand(uint8_t *p, size_t len) { for (size_t i = 0; i < len; i++) p[i] = (uint8_t)xs32(); }

static nand_t NA, NB;   /* 큰 구조체라 정적으로 */
static const nand_geo GEO_TLC = { 2, 4, 8, 64, 8, CELL_TLC };   /* 2 plane × 4 블록 × 8 페이지 × (64 + 8) B */
static const nand_geo GEO_SLC = { 2, 4, 8, 64, 8, CELL_SLC };

/* 블록을 "page 0..k-1 까지 program 된" 상태로 만든다 (데이터 0x00 → 이후 AND 결과를 예측하기 쉽게 0xA5) */
static void prep_block(nand_t *n, uint32_t blk, uint32_t k) {
    uint8_t d[64]; memset(d, 0xA5, sizeof d);
    for (uint32_t p = 0; p < k; p++) (void)nand_program(n, blk, p, d, NULL);
}

int main(void) {
    uint8_t d[64], o[8], rd[64], ro[8];
    memset(d, 0, sizeof d); memset(rd, 0, sizeof rd); memset(ro, 0, sizeof ro);

    /* ===== Part A: 지오메트리 · 셀 표 ===== */
    nand_geo g = GEO_TLC;
    CHECK(nand_init(NULL, &g, NULL, 0) == NAND_ERR_PARAM);
    g.planes = 0;            CHECK(nand_init(&NA, &g, NULL, 0) == NAND_ERR_PARAM);
    g = GEO_TLC; g.planes = 5;               CHECK(nand_init(&NA, &g, NULL, 0) == NAND_ERR_PARAM);
    g = GEO_TLC; g.pages_per_block = 65;     CHECK(nand_init(&NA, &g, NULL, 0) == NAND_ERR_PARAM);
    g = GEO_TLC; g.oob_bytes = 0;            CHECK(nand_init(&NA, &g, NULL, 0) == NAND_ERR_PARAM);
    g = GEO_TLC; g.page_bytes = 2048;        CHECK(nand_init(&NA, &g, NULL, 0) == NAND_ERR_PARAM);  /* 8×8×2112 > 64 KiB */
    uint32_t badidx = 8;                     CHECK(nand_init(&NA, &GEO_TLC, &badidx, 1) == NAND_ERR_PARAM);
    CHECK(nand_init(&NA, &GEO_TLC, NULL, 0) == NAND_OK && NA.nblocks == 8);
    CHECK(CELL[CELL_SLC].endurance == 100000u && CELL[CELL_SLC].nop == 4 && !CELL[CELL_SLC].strict_seq);
    CHECK(CELL[CELL_TLC].endurance == 1500u && CELL[CELL_TLC].nop == 1 && CELL[CELL_TLC].strict_seq);
    printf("cell  bits  P/E      tR   tPROG  tBERS  NOP  order\n");
    for (int c = 0; c < CELL_COUNT; c++)
        printf("%-4s  %u  %7u  %4u  %5u  %5u   %u   %s\n", CELL[c].name, CELL[c].bits, CELL[c].endurance,
               CELL[c].t_r_us, CELL[c].t_prog_us, CELL[c].t_bers_us, CELL[c].nop,
               CELL[c].strict_seq ? "strict" : "skip-fwd");

    /* ===== Part A: erased = 0xFF (전 페이지 전수) ===== */
    unsigned not_ff = 0, rfail = 0;
    for (uint32_t b = 0; b < NA.nblocks; b++)
        for (uint32_t p = 0; p < 8; p++) {
            memset(rd, 0, sizeof rd); memset(ro, 0, sizeof ro);
            rfail += nand_read(&NA, b, p, rd, ro) != NAND_OK;
            for (int i = 0; i < 64; i++) not_ff += rd[i] != 0xFF;
            for (int i = 0; i < 8; i++)  not_ff += ro[i] != 0xFF;
        }
    CHECK(rfail == 0 && not_ff == 0);
    CHECK(NA.n_read == 64 && NA.busy_us[0] == 32u * 70 && NA.busy_us[1] == 32u * 70);

    /* ===== Part A: 전 블록을 순서대로 program → read back ===== */
    static uint8_t ref[8][8][72];
    rng = 0x12B12u;
    unsigned prog_fail = 0, mism = 0;
    for (uint32_t b = 0; b < 8; b++)
        for (uint32_t p = 0; p < 8; p++) {
            fill_rand(ref[b][p], 72);
            ref[b][p][64] = 0xFF;   /* OOB byte 0 은 불량 마커 자리 — 비워 둔다 */
            prog_fail += nand_program(&NA, b, p, ref[b][p], ref[b][p] + 64) != NAND_OK;
        }
    for (uint32_t b = 0; b < 8; b++)
        for (uint32_t p = 0; p < 8; p++) {
            (void)nand_read(&NA, b, p, rd, ro);
            mism += memcmp(rd, ref[b][p], 64) != 0 || memcmp(ro, ref[b][p] + 64, 8) != 0;
        }
    CHECK(prog_fail == 0 && mism == 0 && NA.n_prog == 64);

    /* erase: 그 블록만 0xFF, P/E +1, 쓰기 포인터 리셋 */
    CHECK(nand_erase(&NA, 1) == NAND_OK);
    not_ff = 0;
    for (uint32_t p = 0; p < 8; p++) { (void)nand_read(&NA, 1, p, rd, ro);
        for (int i = 0; i < 64; i++) not_ff += rd[i] != 0xFF;
        for (int i = 0; i < 8; i++)  not_ff += ro[i] != 0xFF; }
    CHECK(not_ff == 0 && NA.pe[1] == 1 && NA.pe[0] == 0 && NA.next_page[1] == 0);
    (void)nand_read(&NA, 2, 7, rd, NULL);
    CHECK(memcmp(rd, ref[2][7], 64) == 0);                      /* 이웃 블록은 그대로 */
    CHECK(nand_program(&NA, 1, 0, ref[1][0], NULL) == NAND_OK);  /* 지운 뒤 다시 page 0 부터 */

    /* 범위 · 파라미터 */
    CHECK(nand_read(&NA, 8, 0, rd, NULL) == NAND_ERR_RANGE);
    CHECK(nand_read(&NA, 0, 8, rd, NULL) == NAND_ERR_RANGE);
    CHECK(nand_program(&NA, 8, 0, d, NULL) == NAND_ERR_RANGE);
    CHECK(nand_program(&NA, 1, 1, NULL, NULL) == NAND_ERR_PARAM);
    CHECK(nand_erase(&NA, 99) == NAND_ERR_RANGE);
    CHECK(NA.next_page[1] == 1);                                 /* 거절된 명령은 상태를 안 바꾼다 */

    /* ===== Part B: 규칙 위반 하나씩 ===== */
    CHECK(nand_init(&NA, &GEO_TLC, NULL, 0) == NAND_OK);
    memset(d, 0x3C, sizeof d);
    CHECK(nand_program(&NA, 0, 0, d, NULL) == NAND_OK);
    uint64_t busy_before = NA.busy_us[0];
    memset(d, 0x00, sizeof d);
    CHECK(nand_program(&NA, 0, 0, d, NULL) == NAND_ERR_NOT_ERASED);       /* TLC NOP=1: 두 번째 program */
    (void)nand_read(&NA, 0, 0, rd, NULL);
    CHECK(rd[0] == 0x3C && rd[63] == 0x3C);                                /* 데이터 그대로 */
    CHECK(NA.busy_us[0] == busy_before + 70);                              /* 거절된 program 은 시간 0, read 만 더해짐 */
    CHECK(nand_program(&NA, 0, 2, d, NULL) == NAND_ERR_OUT_OF_ORDER);      /* TLC: 앞으로 건너뛰기 금지 */
    CHECK(nand_program(&NA, 0, 1, d, NULL) == NAND_OK);

    CHECK(nand_init(&NB, &GEO_SLC, NULL, 0) == NAND_OK);
    CHECK(nand_program(&NB, 0, 0, d, NULL) == NAND_OK);
    CHECK(nand_program(&NB, 0, 3, d, NULL) == NAND_OK);                    /* SLC: 1,2 건너뛰기 허용 (모델 선택) */
    CHECK(nand_program(&NB, 0, 1, d, NULL) == NAND_ERR_OUT_OF_ORDER);      /* 건너뛴 페이지로 되돌아가기 */
    CHECK(nand_program(&NB, 0, 2, d, NULL) == NAND_ERR_OUT_OF_ORDER);
    CHECK(nand_program(&NB, 0, 0, d, NULL) == NAND_ERR_NOT_ERASED);        /* 옛 페이지 재프로그램 */
    CHECK(nand_program(&NB, 0, 3, d, NULL) == NAND_OK);                    /* 마지막 페이지 partial (2/4) */

    /* 판정 표 전수: 블록이 page 0..k-1 까지 차 있을 때 page p 에 program (k = 0..8, p = 0..7) */
    unsigned tbl_bad = 0;
    for (int c = 0; c < 2; c++) {
        const nand_geo *gg = c == 0 ? &GEO_TLC : &GEO_SLC;
        for (uint32_t k = 0; k <= 8; k++)
            for (uint32_t p = 0; p < 8; p++) {
                (void)nand_init(&NA, gg, NULL, 0);
                prep_block(&NA, 0, k);
                nand_rc exp;
                if (c == 0) exp = p < k ? NAND_ERR_NOT_ERASED : p == k ? NAND_OK : NAND_ERR_OUT_OF_ORDER;
                else        exp = (k > 0 && p == k - 1) ? NAND_OK : p < k ? NAND_ERR_NOT_ERASED : NAND_OK;
                nand_rc got = nand_program(&NA, 0, p, d, NULL);
                if (got != exp && tbl_bad++ < 3) printf("  %s k=%u p=%u got %s exp %s\n", CELL[gg->cell].name,
                                                        k, p, RC_NAME[got], RC_NAME[exp]);
            }
    }
    CHECK(tbl_bad == 0);

    /* SLC NOP=4: 같은 페이지에 4번까지 partial program, 결과 = AND. 5번째는 NOT_ERASED */
    (void)nand_init(&NB, &GEO_SLC, NULL, 0);
    unsigned nop_ok = 0;
    for (int q = 0; q < 4; q++) {           /* q 번째 16 B 조각만 0x5A, 나머지는 0xFF ("안 건드림") */
        memset(d, 0xFF, sizeof d); memset(d + 16 * q, 0x5A, 16);
        nop_ok += nand_program(&NB, 0, 0, d, NULL) == NAND_OK;
    }
    (void)nand_read(&NB, 0, 0, rd, NULL);
    unsigned not5a = 0; for (int i = 0; i < 64; i++) not5a += rd[i] != 0x5A;
    CHECK(nop_ok == 4 && not5a == 0);
    CHECK(nand_program(&NB, 0, 0, d, NULL) == NAND_ERR_NOT_ERASED);
    memset(d, 0x0F, sizeof d);              /* AND 확인: 0x5A & 0x0F = 0x0A — partial 은 1→0 만 */
    CHECK(nand_program(&NB, 0, 1, d, NULL) == NAND_OK);
    memset(d, 0xF0, sizeof d);
    CHECK(nand_program(&NB, 0, 1, d, NULL) == NAND_OK);
    (void)nand_read(&NB, 0, 1, rd, NULL);
    CHECK(rd[0] == 0x00 && rd[63] == 0x00); /* 0x0F & 0xF0: 두 번 써서 "OR" 을 만들 수는 없다 */

    /* 마모: 셀 타입마다 정격 P/E 만큼은 성공, 그다음 erase 가 FAIL → 블록 WORN */
    for (int c = 0; c < CELL_COUNT; c++) {
        nand_geo gw = GEO_TLC; gw.cell = (cell_t)c;
        (void)nand_init(&NA, &gw, NULL, 0);
        uint32_t E = CELL[c].endurance, ok = 0;
        memset(d, 0x11, sizeof d);
        for (uint32_t i = 0; i < E; i++) {
            ok += nand_program(&NA, 2, 0, d, NULL) == NAND_OK;
            ok += nand_erase(&NA, 2) == NAND_OK;
        }
        uint64_t b0 = NA.busy_us[0];
        nand_rc r1 = nand_erase(&NA, 2);
        nand_rc r2 = nand_program(&NA, 2, 0, d, NULL);
        nand_rc r3 = nand_erase(&NA, 2);
        printf("wear %s: %u P/E ok, next erase=%s, program=%s, erase=%s, pe=%u\n", CELL[c].name, ok / 2,
               RC_NAME[r1], RC_NAME[r2], RC_NAME[r3], NA.pe[2]);
        CHECK(ok == 2 * E && r1 == NAND_ERR_WORN_OUT && r2 == NAND_ERR_WORN_OUT && r3 == NAND_ERR_WORN_OUT);
        CHECK(NA.pe[2] == E && NA.state[2] == BLK_WORN);
        CHECK(NA.busy_us[0] == b0 + 2u * CELL[c].t_bers_us + CELL[c].t_prog_us);  /* FAIL 도 시간은 쓴다 */
        CHECK(nand_read(&NA, 2, 0, rd, NULL) == NAND_OK);                         /* 읽기는 된다 */
        CHECK(nand_program(&NA, 0, 0, d, NULL) == NAND_OK && NA.pe[0] == 0);      /* 이웃은 멀쩡 */
    }

    /* 공장 불량 블록: program/erase 거부 (시간 0), 읽기는 되고 마커가 보인다 */
    const uint32_t FBAD[2] = { 3, 6 };
    CHECK(nand_init(&NA, &GEO_TLC, FBAD, 2) == NAND_OK);
    memset(d, 0x77, sizeof d);
    CHECK(nand_program(&NA, 3, 0, d, NULL) == NAND_ERR_BAD_BLOCK);
    CHECK(nand_erase(&NA, 6) == NAND_ERR_BAD_BLOCK);
    CHECK(NA.busy_us[0] == 0 && NA.busy_us[1] == 0);
    CHECK(nand_read(&NA, 6, 0, NULL, ro) == NAND_OK && ro[0] == 0x00 && ro[1] == 0xFF);

    /* 바쁜 시간: plane = blk % 2. 정확한 µs 합 */
    (void)nand_init(&NA, &GEO_TLC, NULL, 0);
    (void)nand_program(&NA, 0, 0, d, NULL);   /* p0 +1000 */
    (void)nand_program(&NA, 0, 1, d, NULL);   /* p0 +1000 */
    (void)nand_read(&NA, 0, 1, rd, NULL);     /* p0 +70   */
    (void)nand_erase(&NA, 0);                 /* p0 +3500 */
    (void)nand_program(&NA, 5, 0, d, NULL);   /* p1 +1000 */
    (void)nand_program(&NA, 5, 0, d, NULL);   /* 거절 +0  */
    CHECK(NA.busy_us[0] == 5570u && NA.busy_us[1] == 1000u && makespan(&NA) == 5570u && busy_total(&NA) == 6570u);

    /* ===== Part C: 드라이버 계약 ===== */
    uint8_t bbt[NAND_MAX_BLOCKS];
    CHECK(nand_init(&NA, &GEO_TLC, FBAD, 2) == NAND_OK);
    int nbad = nand_scan_bbt(&NA, bbt, NAND_MAX_BLOCKS);
    CHECK(nbad == 2 && bbt[3] == 1 && bbt[6] == 1 && bbt[0] == 0 && bbt[7] == 0);
    CHECK(NA.n_read == 8 && NA.busy_us[0] == 4u * 70 && NA.busy_us[1] == 4u * 70);  /* 블록당 OOB 읽기 1번 */
    CHECK(nand_scan_bbt(&NA, bbt, 4) == -1);

    /* 함정: 사용자가 양품 블록 page 0 의 OOB byte 0 에 0x00 을 쓰면 → 다음 부팅 스캔이 불량으로 오판 */
    memset(o, 0xFF, sizeof o); o[0] = 0x00;
    (void)nand_init(&NB, &GEO_TLC, FBAD, 2);
    CHECK(nand_program(&NB, 1, 0, d, o) == NAND_OK);
    CHECK(nand_scan_bbt(&NB, bbt, NAND_MAX_BLOCKS) == 3 && bbt[1] == 1);

    /* 워크로드: LBA 0..7 (블록 하나 분량) 채운 뒤 랜덤 갱신 40회. in-place 는 그냥 덮어쓰면 실패한다 */
    static uint8_t want[8][64];
    rng = 0xB12C0DEu;
    (void)nand_init(&NA, &GEO_TLC, FBAD, 2);   /* in-place 칩: 블록 0 사용 */
    (void)nand_init(&NB, &GEO_TLC, FBAD, 2);   /* out-of-place 칩 */
    CHECK(nand_scan_bbt(&NB, bbt, NAND_MAX_BLOCKS) == 2);
    log_t L; log_init(&L, 8, bbt);
    unsigned wfail = 0;
    for (uint32_t i = 0; i < 8; i++) {
        fill_rand(want[i], 64);
        wfail += nand_program(&NA, 0, i, want[i], NULL) != NAND_OK;
        wfail += log_write(&NB, &L, i, want[i]) != NAND_OK;
    }
    CHECK(wfail == 0);
    fill_rand(d, 64);
    CHECK(nand_program(&NA, 0, 5, d, NULL) == NAND_ERR_NOT_ERASED);   /* "그냥 덮어쓰기" 는 안 된다 */

    /* 채우기 이후 증가분만 센다 */
    uint32_t rA0 = NA.n_read, pA0 = NA.n_prog, eA0 = NA.n_erase, rB0 = NB.n_read, pB0 = NB.n_prog, eB0 = NB.n_erase;
    uint64_t bA0[2] = { NA.busy_us[0], NA.busy_us[1] }, bB0[2] = { NB.busy_us[0], NB.busy_us[1] };
    for (int u = 0; u < 40; u++) {
        uint32_t lba = xs32() % 8;
        fill_rand(want[lba], 64);
        wfail += naive_update(&NA, 0, lba, want[lba]) != NAND_OK;
        wfail += log_write(&NB, &L, lba, want[lba]) != NAND_OK;
    }
    CHECK(wfail == 0);
    uint32_t dpA = NA.n_prog - pA0, deA = NA.n_erase - eA0, drA = NA.n_read - rA0;
    uint32_t dpB = NB.n_prog - pB0, deB = NB.n_erase - eB0, drB = NB.n_read - rB0;
    uint64_t A0 = NA.busy_us[0] - bA0[0], A1 = NA.busy_us[1] - bA0[1];
    uint64_t B0 = NB.busy_us[0] - bB0[0], B1 = NB.busy_us[1] - bB0[1];
    uint32_t maxpeA = 0, maxpeB = 0;
    for (uint32_t b = 0; b < 8; b++) { if (NA.pe[b] > maxpeA) maxpeA = NA.pe[b]; if (NB.pe[b] > maxpeB) maxpeB = NB.pe[b]; }
    printf("40 random updates of 8 LBAs (TLC 대표값, 채우기 이후 증가분)\n");
    printf("method        prog  erase  read  busy_p0_us  busy_p1_us  makespan_us  maxPE  stale\n");
    printf("in-place RMW  %4u  %5u  %4u  %10llu  %10llu  %11llu  %5u      -\n", dpA, deA, drA,
           (unsigned long long)A0, (unsigned long long)A1, (unsigned long long)(A0 > A1 ? A0 : A1), maxpeA);
    printf("out-of-place  %4u  %5u  %4u  %10llu  %10llu  %11llu  %5u  %5u\n", dpB, deB, drB,
           (unsigned long long)B0, (unsigned long long)B1, (unsigned long long)(B0 > B1 ? B0 : B1), maxpeB, L.stale);
    CHECK(dpA == 320 && deA == 40 && drA == 320 && A0 == 320u * 1000 + 40u * 3500 + 320u * 70 && A1 == 0);
    CHECK(dpB == 40 && deB == 0 && drB == 0 && maxpeA == 40 && maxpeB == 0 && L.stale == 40);
    CHECK(B0 + B1 == 40u * 1000);

    /* 두 방법 모두 데이터는 맞아야 한다 */
    mism = 0;
    for (uint32_t i = 0; i < 8; i++) {
        (void)nand_read(&NA, 0, i, rd, NULL); mism += memcmp(rd, want[i], 64) != 0;
        (void)log_read(&NB, &L, i, rd);       mism += memcmp(rd, want[i], 64) != 0;
    }
    CHECK(mism == 0);
    /* 로그는 불량 블록 3, 6 을 건너뛰어 양품 6블록 × 8페이지 = 48 페이지를 다 썼다 → 다음 쓰기는 GC 필요 */
    CHECK(L.active == 7 && L.wp == 8);
    CHECK(log_write(&NB, &L, 0, d) == NAND_ERR_RANGE);
    (void)nand_read(&NB, L.map_blk[3], L.map_page[3], NULL, ro);
    CHECK(ro[0] == 0xFF && ro[1] == 3 && ro[2] == 0);                /* OOB 에 P2L (LBA) 기록 */

    /* in-place 만 쓰면: 같은 블록에 갱신 1500 번이면 TLC 블록 하나가 죽는다 */
    (void)nand_init(&NA, &GEO_TLC, NULL, 0);
    for (uint32_t i = 0; i < 8; i++) (void)nand_program(&NA, 0, i, want[i], NULL);
    uint32_t updates = 0;
    nand_rc last;
    while ((last = naive_update(&NA, 0, updates % 8, want[updates % 8])) == NAND_OK) updates++;
    printf("in-place on TLC block 0: %u updates, then %s (pe=%u)\n", updates, RC_NAME[last], NA.pe[0]);
    CHECK(updates == 1500 && last == NAND_ERR_WORN_OUT && NA.state[0] == BLK_WORN);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
