/* BM-A02 추가 연습 — 한 번에 검증
 * 연습 1: drv_write 의 program 횟수 닫힌 식       연습 2: F4 섹터 번호를 루프 없이
 * 연습 3: erase 완료 마커로 "blank 이면 건너뛰기" 를 안전하게 (모든 op 전원 차단 × 64 seed)
 * 연습 4: ECC 플래시(8 B 1회 프로그램)에서 commit 마지막 + 묘비(tombstone) 로 무효화
 * 아래 모델·드라이버·CRC 는 solution.c 와 같은 코드 (BM-A02 플래시 모델 전체판).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 플래시 모델 (BM-A02 소유 — 전체판) ======================= */
#define FLASH_MAX          (1024u * 1024u)    /* 1 MB (STM32F407VG 크기) */
#define FLASH_MAX_SECTORS  16u
#define FLASH_MAX_UNIT     8u

typedef struct {
    uint32_t base;            /* e.g. 0x08000000 */
    uint32_t sector_size;     /* sizes == NULL 일 때의 균일 섹터 크기 */
    uint32_t n_sectors;
    uint32_t prog_unit;       /* bytes per program operation (1, 4 or 8); address must be aligned to it */
    uint8_t  mem[FLASH_MAX];  /* erased = 0xFF; program does mem &= data (1→0 only) */
    uint32_t n_erase, n_prog; /* op counters (wear, cost) */
    uint32_t op_index;        /* incremented per erase/program call — power-cut hook */
    uint32_t cut_at;          /* 0 = never; when op_index == cut_at the op is torn and FLASH_POWER_CUT returned */
    uint32_t rng;             /* xorshift32 state for undefined bits after a torn op */
    /* ---- 여기부터는 전체판에만 있다 (A05/A06 축약판에는 없음) ---- */
    const uint32_t *sizes;    /* F4 식 불균일 섹터 크기 표 (n_sectors 개) 또는 NULL */
    bool     one_shot;        /* ECC 식: 단위가 전부 0xFF 일 때만 프로그램 가능 (아니면 NOT_ERASED) */
    uint32_t erase_count[FLASH_MAX_SECTORS];   /* 섹터별 wear */
    uint8_t  weak[FLASH_MAX]; /* 1 로 읽히지만 약하게만 지워진 비트 (torn erase). flash_age() 때 0 으로 */
    uint32_t stuck_off;       /* 고장 셀 주입: 오프셋, 비트, 레벨 (mask 0 = 없음) */
    uint8_t  stuck_mask;
    uint8_t  stuck_level;     /* 1 = program 이 못 내림 (1 고착), 0 = erase 가 못 올림 (0 고착) */
    uint32_t last_tear;       /* 마지막 torn op 종류: 0..3 = erase 단계, 4 = program */
} flash_t;

typedef enum { FLASH_OK = 0, FLASH_ERR_ALIGN, FLASH_ERR_RANGE, FLASH_ERR_NOT_ERASED, FLASH_ERR_VERIFY,
               FLASH_POWER_CUT } flash_rc_t;

uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

uint32_t flash_sector_len(const flash_t *f, uint32_t s) { return f->sizes ? f->sizes[s] : f->sector_size; }
uint32_t flash_sector_start(const flash_t *f, uint32_t s) {          /* base 로부터의 오프셋 */
    uint32_t o = 0;
    for (uint32_t i = 0; i < s; i++) o += flash_sector_len(f, i);
    return o;
}
uint32_t flash_total(const flash_t *f) { return flash_sector_start(f, f->n_sectors); }
int flash_sector_of(const flash_t *f, uint32_t addr) {               /* 범위 밖이면 -1 */
    if (addr < f->base) return -1;
    uint32_t off = addr - f->base, o = 0;
    for (uint32_t s = 0; s < f->n_sectors; s++) {
        uint32_t l = flash_sector_len(f, s);
        if (off - o < l) return (int)s;
        o += l;
    }
    return -1;
}
bool flash_in_range(const flash_t *f, uint32_t addr, uint32_t len, uint32_t *off) {   /* 오버플로 안전 */
    uint32_t total = flash_total(f);
    if (addr < f->base) return false;
    uint32_t o = addr - f->base;
    if (o > total || len > total - o) return false;
    *off = o;
    return true;
}
bool flash_init(flash_t *f, uint32_t base, const uint32_t *sizes, uint32_t sector_size, uint32_t n_sectors,
                uint32_t prog_unit) {
    f->base = base; f->sizes = sizes; f->sector_size = sector_size; f->n_sectors = n_sectors;
    f->prog_unit = prog_unit;
    f->n_erase = f->n_prog = f->op_index = f->cut_at = 0;
    f->rng = 0x2545F491u; f->one_shot = false; f->stuck_off = 0; f->stuck_mask = 0; f->stuck_level = 1;
    f->last_tear = 0;
    memset(f->erase_count, 0, sizeof f->erase_count);
    if (n_sectors == 0 || n_sectors > FLASH_MAX_SECTORS) return false;
    if (prog_unit != 1 && prog_unit != 4 && prog_unit != 8) return false;
    uint64_t total = 0;
    for (uint32_t s = 0; s < n_sectors; s++) {
        uint32_t l = sizes ? sizes[s] : sector_size;
        if (l == 0 || l % prog_unit) return false;
        total += l;
    }
    if (total > FLASH_MAX) return false;
    memset(f->mem, 0xFF, (size_t)total);                             /* 출하 상태 = 지워짐 */
    memset(f->weak, 0, (size_t)total);
    return true;
}
void flash_power_on(flash_t *f) { f->cut_at = 0; }                    /* 재부팅: 셀 상태는 그대로 */
void flash_age(flash_t *f) {                                         /* 시간이 흐름: 약한 비트가 0 으로 */
    uint32_t total = flash_total(f);
    for (uint32_t o = 0; o < total; o++) { f->mem[o] &= (uint8_t)~f->weak[o]; f->weak[o] = 0; }
}
void flash_read(const flash_t *f, uint32_t addr, uint8_t *dst, uint32_t len) {
    uint32_t off;
    if (!flash_in_range(f, addr, len, &off)) { memset(dst, 0, len); return; }   /* 실제 HW 는 BusFault */
    memcpy(dst, f->mem + off, len);
}
bool flash_dead(const flash_t *f) { return f->cut_at != 0 && f->op_index >= f->cut_at; }
bool flash_tick(flash_t *f) { f->op_index++; return f->op_index == f->cut_at; }   /* true = 이 op 가 찢어진다 */
uint8_t stuck_bits(const flash_t *f, uint32_t off, uint8_t level) {
    return (off == f->stuck_off && f->stuck_level == level) ? f->stuck_mask : 0;
}
/* torn erase: 단계(phase)를 rng 로 고른다.  erase 는 비트를 0→1 로만 움직인다 */
void tear_erase(flash_t *f, uint32_t off, uint32_t len) {
    uint32_t phase = xorshift32(&f->rng) % 4u;
    f->last_tear = phase;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t o = off + i;
        uint8_t r1 = (uint8_t)xorshift32(&f->rng), r2 = (uint8_t)(xorshift32(&f->rng) >> 8);
        switch (phase) {
        case 0: f->mem[o] |= (uint8_t)(r1 & r2); break;               /* 초반: 1/4 쯤만 1 로 */
        case 1: f->mem[o] |= r1; break;                                /* 중반: 절반쯤 */
        case 2: f->weak[o] |= (uint8_t)(~f->mem[o] & r1 & r2);          /* 후반: 전부 1 로 읽히지만 */
                f->mem[o] = 0xFF; break;                               /*       일부는 약하게만 지워짐 */
        default: f->mem[o] = 0xFF; f->weak[o] = 0; break;               /* 끝났는데 CPU 가 EOP 를 못 봄 */
        }
        f->mem[o] &= (uint8_t)~stuck_bits(f, o, 0);
    }
}
/* torn program: 이 단위에서 일어나야 할 1→0 전이 중 무작위 부분집합만 일어났다 */
void tear_program(flash_t *f, uint32_t off, const uint8_t *src, uint32_t n) {
    f->last_tear = 4;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t o = off + i;
        uint8_t want = (uint8_t)(f->mem[o] & ~(src[i] | stuck_bits(f, o, 1)));
        f->mem[o] &= (uint8_t)~(want & (uint8_t)xorshift32(&f->rng));
    }
}

/* ---- Part A 모델: erase / program ---- */
flash_rc_t flash_erase_sector(flash_t *f, uint32_t sector) {
    /* A2: 전원이 이미 나갔으면 POWER_CUT. 섹터 번호 검사(RANGE). op 를 센다(flash_tick) —
     *          wear 카운터(n_erase, erase_count[s])도 올린다. 찢어지면 tear_erase 후 POWER_CUT.
     *          정상이면 섹터 전체 0xFF, weak 비트 0, 0 고착 셀(stuck_level 0)은 0 으로 남는다. */
    if (flash_dead(f)) return FLASH_POWER_CUT;
    if (sector >= f->n_sectors) return FLASH_ERR_RANGE;
    const uint32_t off = flash_sector_start(f, sector), len = flash_sector_len(f, sector);
    const bool torn = flash_tick(f);
    f->n_erase++;
    f->erase_count[sector]++;
    if (torn) { tear_erase(f, off, len); return FLASH_POWER_CUT; }
    memset(f->mem + off, 0xFF, len);
    memset(f->weak + off, 0, len);
    for (uint32_t i = 0; i < len; i++) f->mem[off + i] &= (uint8_t)~stuck_bits(f, off + i, 0);
    return FLASH_OK;
}

flash_rc_t flash_program(flash_t *f, uint32_t addr, const uint8_t *src, uint32_t len) {
    /* A1: 전원 검사 → 범위(RANGE) → 정렬(addr, len 모두 prog_unit 배수, 아니면 ALIGN) →
     *          one_shot 이면 대상 바이트가 전부 0xFF 여야(NOT_ERASED) → flash_tick + n_prog++ →
     *          단위마다 mem &= (src | 1 고착 비트).  찢어지는 op 는 rng 로 고른 단위 하나를
     *          tear_program 하고 POWER_CUT (그 앞 단위는 완료, 뒤 단위는 안 됨). */
    if (flash_dead(f)) return FLASH_POWER_CUT;
    uint32_t off;
    if (!flash_in_range(f, addr, len, &off)) return FLASH_ERR_RANGE;
    const uint32_t u = f->prog_unit;
    if (off % u != 0 || len % u != 0) return FLASH_ERR_ALIGN;
    if (len == 0) return FLASH_OK;
    if (f->one_shot)
        for (uint32_t i = 0; i < len; i++)
            if (f->mem[off + i] != 0xFF) return FLASH_ERR_NOT_ERASED;
    const bool torn = flash_tick(f);
    f->n_prog++;
    const uint32_t n_units = len / u;
    const uint32_t cut_unit = torn ? xorshift32(&f->rng) % n_units : n_units;
    for (uint32_t k = 0; k < n_units; k++) {
        const uint32_t o = off + k * u;
        if (k == cut_unit) { tear_program(f, o, src + k * u, u); return FLASH_POWER_CUT; }
        for (uint32_t i = 0; i < u; i++)
            f->mem[o + i] &= (uint8_t)(src[k * u + i] | stuck_bits(f, o + i, 1));
    }
    return FLASH_OK;
}

/* ======================= 드라이버 ======================= */
bool drv_blank_check(const flash_t *f, uint32_t addr, uint32_t len) {
    uint8_t buf[64];
    while (len) {
        uint32_t n = len < 64 ? len : 64;
        flash_read(f, addr, buf, n);
        for (uint32_t i = 0; i < n; i++)
            if (buf[i] != 0xFF) return false;
        addr += n; len -= n;
    }
    return true;
}

flash_rc_t drv_write(flash_t *f, uint32_t addr, const uint8_t *buf, uint32_t len) {
    /* A3: len 0 → OK, 범위 → RANGE.  쓰기 범위를 감싸는 단위 [u_first, u_end) 를 구한다.
     *          (1) 사전검사: 내 바이트마다 (old & new) == new, one_shot 이면 단위 전체가 0xFF —
     *              하나라도 어기면 아무것도 쓰지 않고 NOT_ERASED.
     *          (2) 단위마다: 현재 내용을 읽어 내 바이트만 덮어쓰고(read-merge) flash_program,
     *              rc != OK 면 그대로 반환, 다시 읽어 비교 → 다르면 VERIFY. */
    uint32_t off;
    if (len == 0) return FLASH_OK;
    if (!flash_in_range(f, addr, len, &off)) return FLASH_ERR_RANGE;
    const uint32_t u = f->prog_unit, end = off + len;
    const uint32_t u_first = off - off % u, u_end = (end + u - 1) / u * u;
    uint8_t cur[FLASH_MAX_UNIT], rb[FLASH_MAX_UNIT];
    for (uint32_t uo = u_first; uo < u_end; uo += u) {               /* (1) 전부 아니면 전무 */
        flash_read(f, f->base + uo, cur, u);
        for (uint32_t k = 0; k < u; k++) {
            const uint32_t o = uo + k;
            if (o >= off && o < end && (cur[k] & buf[o - off]) != buf[o - off]) return FLASH_ERR_NOT_ERASED;
            if (f->one_shot && cur[k] != 0xFF) return FLASH_ERR_NOT_ERASED;
        }
    }
    for (uint32_t uo = u_first; uo < u_end; uo += u) {               /* (2) merge → program → verify */
        flash_read(f, f->base + uo, cur, u);
        for (uint32_t k = 0; k < u; k++) {
            const uint32_t o = uo + k;
            if (o >= off && o < end) cur[k] = buf[o - off];
        }
        flash_rc_t rc = flash_program(f, f->base + uo, cur, u);
        if (rc != FLASH_OK) return rc;
        flash_read(f, f->base + uo, rb, u);
        if (memcmp(rb, cur, u) != 0) return FLASH_ERR_VERIFY;
    }
    return FLASH_OK;
}

flash_rc_t drv_erase_range(flash_t *f, uint32_t addr, uint32_t len) {
    /* A4: len 0 → OK, 범위 → RANGE.  addr 는 섹터 시작이어야(아니면 ALIGN — 앞쪽 확장 금지),
     *          끝은 addr+len-1 이 든 섹터의 끝까지 확장.  섹터마다 erase → blank check → 아니면 VERIFY. */
    uint32_t off;
    if (len == 0) return FLASH_OK;
    if (!flash_in_range(f, addr, len, &off)) return FLASH_ERR_RANGE;
    const int s0 = flash_sector_of(f, addr), s1 = flash_sector_of(f, addr + len - 1);
    if (s0 < 0 || s1 < 0) return FLASH_ERR_RANGE;
    if (flash_sector_start(f, (uint32_t)s0) != off) return FLASH_ERR_ALIGN;
    for (uint32_t s = (uint32_t)s0; s <= (uint32_t)s1; s++) {
        flash_rc_t rc = flash_erase_sector(f, s);
        if (rc != FLASH_OK) return rc;
        if (!drv_blank_check(f, f->base + flash_sector_start(f, s), flash_sector_len(f, s)))
            return FLASH_ERR_VERIFY;
    }
    return FLASH_OK;
}

/* ======================= Part B: 레코드 + 각본 시퀀스 + 부팅 복구 ======================= */
uint32_t crc32_calc(const uint8_t *p, uint32_t n) {                  /* CRC-32/ISO-HDLC, bitwise (ALGO-M01) */
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
#define REC_MAX_PAYLOAD 64u
/* 레코드 = payload[n] + CRC-32(payload) LE 4 B */
flash_rc_t rec_write(flash_t *f, uint32_t addr, const uint8_t *payload, uint32_t n) {
    uint8_t b[REC_MAX_PAYLOAD + 4];
    if (n > REC_MAX_PAYLOAD) return FLASH_ERR_RANGE;
    memcpy(b, payload, n);
    uint32_t c = crc32_calc(payload, n);
    for (int i = 0; i < 4; i++) b[n + (uint32_t)i] = (uint8_t)(c >> (8 * i));
    return drv_write(f, addr, b, n + 4);
}
bool rec_valid(const flash_t *f, uint32_t addr, uint32_t n) {
    /* B1: payload n B + CRC 4 B 를 flash_read 로 읽고 crc32_calc(payload) == 저장된 CRC(LE) 인지. */
    uint8_t b[REC_MAX_PAYLOAD + 4];
    if (n > REC_MAX_PAYLOAD) return false;
    flash_read(f, addr, b, n + 4);
    uint32_t stored = (uint32_t)b[n] | (uint32_t)b[n + 1] << 8 | (uint32_t)b[n + 2] << 16 | (uint32_t)b[n + 3] << 24;
    return crc32_calc(b, n) == stored;
}

/* ======================= 추가 연습 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static flash_t g_f;
static const uint32_t F4_SIZES[12] = { 16384, 16384, 16384, 16384, 65536,
    131072, 131072, 131072, 131072, 131072, 131072, 131072 };

/* ---- 연습 1: drv_write 가 부르는 program 횟수의 닫힌 식 ---- */
uint32_t prog_ops(uint32_t off, uint32_t len, uint32_t u) {
    if (len == 0) return 0;
    return (off + len + u - 1) / u - off / u;        /* ⌈(off+len)/u⌉ − ⌊off/u⌋ */
}
static void ex1(void) {
    static const uint32_t units[3] = { 1, 4, 8 };
    uint32_t bad = 0, cases = 0;
    for (int ui = 0; ui < 3; ui++)
        for (uint32_t off = 0; off < 64; off++)
            for (uint32_t len = 0; len <= 64; len++) {
                flash_init(&g_f, 0x08000000u, NULL, 256, 1, units[ui]);
                uint8_t d[64];
                memset(d, 0x00, sizeof d);
                uint32_t p0 = g_f.n_prog;
                if (drv_write(&g_f, 0x08000000u + off, d, len) != FLASH_OK) bad++;
                if (g_f.n_prog - p0 != prog_ops(off, len, units[ui])) bad++;
                cases++;
            }
    CHECK(bad == 0 && cases == 3u * 64u * 65u);
    CHECK(prog_ops(2, 1, 4) == 1 && prog_ops(3, 2, 4) == 2 && prog_ops(250, 20, 4) == 6 && prog_ops(300, 37, 4) == 10);
}

/* ---- 연습 2: F4 섹터 번호를 루프 없이 ---- */
int f4_sector_of(uint32_t off) {                      /* off = addr − 0x08000000, 1 MB 부품 */
    if (off >= 0x100000u) return -1;
    if (off < 0x10000u) return (int)(off >> 14);      /* 16 KB × 4 */
    if (off < 0x20000u) return 4;                     /* 64 KB */
    return (int)(4u + (off >> 17));                   /* 128 KB: 0x20000 → 5 */
}
static void ex2(void) {
    flash_init(&g_f, 0x08000000u, F4_SIZES, 0, 12, 4);
    uint32_t bad = 0;
    for (uint32_t off = 0; off < 0x100000u; off++)
        if (f4_sector_of(off) != flash_sector_of(&g_f, 0x08000000u + off)) bad++;
    CHECK(bad == 0);
    CHECK(f4_sector_of(0x100000u) == -1 && f4_sector_of(0xFFFFFFFFu) == -1);
}

/* ---- 연습 3: erase 완료 마커 — "blank 이면 건너뛰기" 를 안전하게 ---- */
#define P_BASE 0x08000000u
#define P_SEC  256u
#define P_RA   (P_BASE + 1u * P_SEC + 40u)
#define P_RA_N 16u
#define P_RB   (P_BASE + 2u * P_SEC + 44u)
#define P_RB_N 33u
static const uint8_t MARK[8] = { 'E', 'R', 'A', 'S', 'E', 'D', 'O', 'K' };
static uint8_t pa[P_RA_N], pb[P_RB_N];
static uint32_t g_skipped;

static bool marker_ok(const flash_t *f, uint32_t s) {
    uint8_t m[8];
    flash_read(f, f->base + s * P_SEC, m, 8);
    return memcmp(m, MARK, 8) == 0;
}
/* 마커가 유효하고(= 마지막 erase 가 끝까지 갔고 그 뒤에 마커를 썼다) 나머지가 비었으면 건너뛴다 */
flash_rc_t prepare_marked(flash_t *f, uint32_t s) {
    const uint32_t a = f->base + s * P_SEC;
    if (marker_ok(f, s) && drv_blank_check(f, a + 8, P_SEC - 8)) { g_skipped++; return FLASH_OK; }
    flash_rc_t rc = drv_erase_range(f, a, P_SEC);
    if (rc != FLASH_OK) return rc;
    return drv_write(f, a, MARK, 8);                  /* 마커는 erase 가 OK 를 돌려준 "뒤에만" */
}
static flash_rc_t store_marked(flash_t *f) {
    flash_rc_t rc;
    if ((rc = prepare_marked(f, 1)) != FLASH_OK) return rc;
    if ((rc = prepare_marked(f, 2)) != FLASH_OK) return rc;
    if ((rc = rec_write(f, P_RA, pa, P_RA_N)) != FLASH_OK) return rc;
    return rec_write(f, P_RB, pb, P_RB_N);
}
static void init_prev_gen(flash_t *f, uint32_t seed) {   /* 이전 세대: 마커 + 옛 레코드가 있는 섹터 */
    flash_init(f, P_BASE, NULL, P_SEC, 4, 4);
    uint8_t old[P_RB_N];
    for (uint32_t i = 0; i < P_RB_N; i++) old[i] = (uint8_t)(seed * 13u + i * 101u);
    (void)drv_write(f, P_BASE + 1 * P_SEC, MARK, 8);
    (void)drv_write(f, P_BASE + 2 * P_SEC, MARK, 8);
    (void)rec_write(f, P_RA, old, P_RA_N);
    (void)rec_write(f, P_RB, old, P_RB_N);
    f->n_erase = f->n_prog = f->op_index = 0;
    f->rng = seed * 0x9E3779B9u + 77u;
}
static void ex3(void) {
    flash_t *f = &g_f;
    for (uint32_t i = 0; i < P_RA_N; i++) pa[i] = (uint8_t)(0xC0 + i);
    for (uint32_t i = 0; i < P_RB_N; i++) pb[i] = (uint8_t)(0x11 * i + 5);
    init_prev_gen(f, 0);
    CHECK(store_marked(f) == FLASH_OK);
    const uint32_t n_ops = f->op_index;
    uint32_t scen = 0, fail = 0, weak_seen = 0;
    g_skipped = 0;
    for (uint32_t k = 1; k <= n_ops; k++)
        for (uint32_t sd = 1; sd <= 64; sd++) {
            init_prev_gen(f, sd);
            f->cut_at = k;
            if (store_marked(f) != FLASH_POWER_CUT) fail++;
            flash_power_on(f);
            for (uint32_t o = 0; o < 4 * P_SEC; o++) if (f->weak[o]) { weak_seen++; break; }
            if (!(rec_valid(f, P_RA, P_RA_N) && rec_valid(f, P_RB, P_RB_N)))
                if (store_marked(f) != FLASH_OK) fail++;
            flash_age(f);
            if (!(rec_valid(f, P_RA, P_RA_N) && rec_valid(f, P_RB, P_RB_N))) fail++;
            scen++;
        }
    CHECK(n_ops == 2 + 2 + 2 + 5 + 10);               /* erase 2 + 마커 2 (×2 단위) + A 5 + B 10 */
    CHECK(fail == 0 && weak_seen > 0);
    CHECK(g_skipped > 0);                              /* 복구 중 erase 를 실제로 아꼈다 */
    printf("ex3: %u ops x 64 seeds = %u scenarios, weak-erased cases %u, erases skipped by marker %u, failures %u\n",
           n_ops, scen, weak_seen, g_skipped, fail);
}

/* ---- 연습 4: ECC 플래시(8 B, 1회 프로그램)에서 레코드 무효화 ---- */
/* 레이아웃: data[16] | commit[8] = 'C' 'M' crc32(data, LE) 0 0 | tomb[8] (무효화용, 처음엔 0xFF) */
#define E_REC 32u
flash_rc_t erec_write(flash_t *f, uint32_t addr, const uint8_t data[16]) {
    flash_rc_t rc = drv_write(f, addr, data, 16);
    if (rc != FLASH_OK) return rc;
    uint8_t c[8] = { 'C', 'M', 0, 0, 0, 0, 0, 0 };
    uint32_t crc = crc32_calc(data, 16);
    for (int i = 0; i < 4; i++) c[2 + i] = (uint8_t)(crc >> (8 * i));
    return drv_write(f, addr + 16, c, 8);             /* commit 은 data 가 다 써진 뒤 마지막에 */
}
bool erec_valid(const flash_t *f, uint32_t addr) {
    uint8_t b[E_REC];
    flash_read(f, addr, b, E_REC);
    uint32_t crc = (uint32_t)b[18] | (uint32_t)b[19] << 8 | (uint32_t)b[20] << 16 | (uint32_t)b[21] << 24;
    if (b[16] != 'C' || b[17] != 'M' || b[22] != 0 || b[23] != 0 || crc32_calc(b, 16) != crc) return false;
    for (int i = 24; i < 32; i++) if (b[i] != 0xFF) return false;      /* 묘비(tombstone)가 비어 있어야 */
    return true;
}
flash_rc_t erec_invalidate(flash_t *f, uint32_t addr) {
    static const uint8_t z[8] = { 0 };
    return drv_write(f, addr + 24, z, 8);             /* 아직 0xFF 인 단위에만 쓴다 */
}
static void ex4(void) {
    flash_t *f = &g_f;
    uint8_t d[16];
    for (int i = 0; i < 16; i++) d[i] = (uint8_t)(0x5A ^ i);
    flash_init(f, 0x08000000u, NULL, 256, 2, 8);
    f->one_shot = true;
    CHECK(erec_write(f, 0x08000000u, d) == FLASH_OK && erec_valid(f, 0x08000000u));
    uint8_t flag0 = 0x00;                              /* AND 플래시에서 흔한 "플래그 내리기" */
    CHECK(drv_write(f, 0x08000016u, &flag0, 1) == FLASH_ERR_NOT_ERASED);   /* commit 단위 재프로그램 거절 */
    CHECK(erec_valid(f, 0x08000000u));
    CHECK(erec_invalidate(f, 0x08000000u) == FLASH_OK && !erec_valid(f, 0x08000000u));
    CHECK(erec_invalidate(f, 0x08000000u) == FLASH_ERR_NOT_ERASED);       /* 묘비도 한 번뿐 */
    /* 쓰기 중 모든 op 에서 전원 차단: 유효 판정이면 내용도 맞다 */
    uint32_t bad = 0, valid_after_cut = 0;
    for (uint32_t k = 1; k <= 3; k++)                  /* data 2 단위 + commit 1 단위 = 3 op */
        for (uint32_t sd = 1; sd <= 500; sd++) {
            flash_init(f, 0x08000000u, NULL, 256, 2, 8);
            f->one_shot = true; f->rng = sd; f->cut_at = k;
            if (erec_write(f, 0x08000040u, d) != FLASH_POWER_CUT) bad++;
            flash_power_on(f);
            if (erec_valid(f, 0x08000040u)) {
                valid_after_cut++;
                uint8_t got[16];
                flash_read(f, 0x08000040u, got, 16);
                if (memcmp(got, d, 16) != 0) bad++;
            }
            if (k < 3 && erec_valid(f, 0x08000040u)) bad++;   /* commit 전에 끊기면 절대 유효 아님 */
        }
    CHECK(bad == 0);
    printf("ex4: 1500 cut scenarios, valid after cut %u (only when the commit unit itself completed)\n",
           valid_after_cut);
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
