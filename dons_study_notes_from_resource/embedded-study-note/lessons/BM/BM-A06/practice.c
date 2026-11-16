/* BM-A06 추가 연습 — 버그 사냥(연습 1~3), append-only 로그 메타(연습 4), scratch 수명(연습 5)
 *
 * 본문 solution.c 의 모델·알고리즘을 그대로 복사하고 연습용 손잡이(g_bug, g_meta_log)만 달았다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAYBE_UNUSED __attribute__((unused))   /* 시작 코드(스텁)에서 아직 안 쓰는 도우미의 경고 끄기 */

/* ===== 연습용 손잡이 ===== */
enum { BUG_NONE = 0, BUG_STATUS_FIRST, BUG_SINGLE_COPY, BUG_NO_CRC };
static int  g_bug = BUG_NONE;      /* 연습 1~3: 버그 주입 */
static bool g_meta_log = false;    /* 연습 4: append-only 로그 메타 */

/* ======================= (BM-A02 플래시 모델 축약판) ======================= */
#define FLASH_BASE   0x08000000u
#define FLASH_MAX    4096u
#define MAX_SECTORS  16u
#define TRACE_MAX    1024u

typedef struct {
    uint32_t base;            /* 0x08000000 */
    uint32_t sector_size;     /* 축약판: 균일 섹터 */
    uint32_t n_sectors;
    uint32_t prog_unit;       /* program 단위 (8 B), 주소·길이 정렬 필요 */
    uint8_t  mem[FLASH_MAX];  /* erased = 0xFF, program 은 mem &= data (1→0 만) */
    uint32_t n_erase, n_prog; /* op 카운터 */
    uint32_t op_index;        /* erase/program 호출마다 +1 — 전원 차단 훅 */
    uint32_t cut_at;          /* 0 = 없음, op_index == cut_at 인 op 에서 FLASH_POWER_CUT */
    uint32_t rng;             /* 찢긴(torn) op 의 비트를 정하는 xorshift32 */
    /* --- A06 추가 --- */
    uint32_t cut_mode;                 /* CUT_TORN / CUT_BEFORE / CUT_AFTER */
    bool     dead;                     /* 차단 뒤 flash_power_on 전까지 모든 op = FLASH_POWER_CUT */
    uint32_t erase_cnt[MAX_SECTORS];   /* 섹터별 erase 횟수 (마모) */
    uint64_t prog_bytes[MAX_SECTORS];  /* 섹터별 program 바이트 */
    uint16_t trace[TRACE_MAX];         /* op 기록: (종류 << 8) | 섹터 */
} flash_t;

typedef enum { FLASH_OK = 0, FLASH_ERR_ALIGN, FLASH_ERR_RANGE, FLASH_ERR_NOT_ERASED, FLASH_ERR_VERIFY,
               FLASH_POWER_CUT } flash_rc_t;
enum { CUT_TORN = 0, CUT_BEFORE = 1, CUT_AFTER = 2 };  /* 찢김 / op 직전 / op 직후 */
enum { OP_ERASE = 1, OP_PROG = 2 };

flash_rc_t flash_erase_sector(flash_t *f, uint32_t sector);
flash_rc_t flash_program(flash_t *f, uint32_t addr, const uint8_t *src, uint32_t len);
void       flash_read(const flash_t *f, uint32_t addr, uint8_t *dst, uint32_t len);
void       flash_power_on(flash_t *f);

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static void flash_init(flash_t *f, uint32_t sector_size, uint32_t n_sectors, uint32_t prog_unit) {
    memset(f, 0, sizeof *f);
    f->base = FLASH_BASE; f->sector_size = sector_size; f->n_sectors = n_sectors; f->prog_unit = prog_unit;
    memset(f->mem, 0xFF, sizeof f->mem);
    f->rng = 0x12345678u;
}

void flash_power_on(flash_t *f) { f->dead = false; f->cut_at = 0; }   /* "재부팅": 차단 해제 */

/* op 하나 시작. 이 op 가 차단 지점이면 true (dead 로 전환) */
static bool op_begin(flash_t *f, unsigned kind, uint32_t sector) {
    f->op_index++;
    if (f->op_index <= TRACE_MAX) f->trace[f->op_index - 1] = (uint16_t)((kind << 8) | sector);
    if (f->cut_at != 0 && f->op_index == f->cut_at) { f->dead = true; return true; }
    return false;
}

flash_rc_t flash_erase_sector(flash_t *f, uint32_t sector) {
    if (f->dead) return FLASH_POWER_CUT;
    if (sector >= f->n_sectors) return FLASH_ERR_RANGE;
    uint8_t *p = &f->mem[sector * f->sector_size];
    bool cut = op_begin(f, OP_ERASE, sector);
    if (cut && f->cut_mode == CUT_BEFORE) return FLASH_POWER_CUT;
    f->n_erase++; f->erase_cnt[sector]++;
    if (cut && f->cut_mode == CUT_TORN) {            /* 찢긴 erase: 일부 0xFF, 일부 옛값, 일부 무작위 */
        for (uint32_t i = 0; i < f->sector_size; i++) {
            uint32_t r = xs32(&f->rng);
            if ((r & 3u) <= 1u) p[i] = 0xFF;
            else if ((r & 3u) == 3u) p[i] = (uint8_t)(r >> 8);
        }
        return FLASH_POWER_CUT;
    }
    memset(p, 0xFF, f->sector_size);
    return cut ? FLASH_POWER_CUT : FLASH_OK;          /* CUT_AFTER: 다 지웠지만 CPU 는 결과를 못 봄 */
}

flash_rc_t flash_program(flash_t *f, uint32_t addr, const uint8_t *src, uint32_t len) {
    if (f->dead) return FLASH_POWER_CUT;
    uint32_t total = f->sector_size * f->n_sectors;
    if (addr < f->base || len == 0 || len > total || addr - f->base > total - len) return FLASH_ERR_RANGE;
    uint32_t off = addr - f->base;
    if (off % f->prog_unit != 0 || len % f->prog_unit != 0) return FLASH_ERR_ALIGN;
    uint32_t sector = off / f->sector_size;
    bool cut = op_begin(f, OP_PROG, sector);
    if (cut && f->cut_mode == CUT_BEFORE) return FLASH_POWER_CUT;
    f->n_prog++; f->prog_bytes[sector] += len;
    uint32_t done = len;
    if (cut && f->cut_mode == CUT_TORN) {            /* 앞 unit 들은 완료, 끊긴 unit 은 1→0 일부만, 뒤는 그대로 */
        uint32_t units = len / f->prog_unit, u = xs32(&f->rng) % units;
        done = u * f->prog_unit;
        for (uint32_t i = done; i < done + f->prog_unit; i++) {
            uint8_t want = (uint8_t)(f->mem[off + i] & (uint8_t)~src[i]);   /* 1→0 으로 바뀌어야 할 비트 */
            f->mem[off + i] &= (uint8_t)~(want & (uint8_t)xs32(&f->rng));
        }
    }
    for (uint32_t i = 0; i < done; i++) f->mem[off + i] &= src[i];
    return cut ? FLASH_POWER_CUT : FLASH_OK;
}

void flash_read(const flash_t *f, uint32_t addr, uint8_t *dst, uint32_t len) {
    memcpy(dst, &f->mem[addr - f->base], len);
}

/* ======================= CRC-32/ISO-HDLC (zlib) ======================= */
static uint32_t crc32_ieee(const void *data, uint32_t len) {
    static uint32_t tab[256];
    static bool init;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            tab[i] = c;
        }
        init = true;
    }
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    while (len--) c = tab[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ======================= (BM-A04 헤더 축약판) — CRC 만, SHA-256 생략(flags bit0 = 0) ======================= */
#define IMG_MAGIC     0x474D4942u   /* 메모리 바이트 'B','I','M','G' */
#define IMG_HDR_SIZE  64u
typedef struct {
    uint32_t magic;          /* 0x474D4942 */
    uint16_t hdr_version;    /* 1 */
    uint16_t hdr_size;       /* 64 */
    uint32_t img_version;    /* major<<24 | minor<<16 | patch */
    uint32_t payload_size;   /* 헤더 뒤 바이트 수 */
    uint32_t load_addr;      /* payload(벡터 테이블)가 있어야 할 주소 */
    uint32_t flags;          /* bit0 = SHA256 있음 — 축약판은 0 */
    uint32_t payload_crc32;  /* payload 의 CRC-32 */
    uint8_t  sha256[32];     /* 축약판: 0 */
    uint32_t hdr_crc32;      /* [0, 60) 의 CRC-32 */
} img_hdr_t;
_Static_assert(sizeof(img_hdr_t) == 64, "header must be 64 bytes");
_Static_assert(offsetof(img_hdr_t, hdr_crc32) == 60, "hdr_crc32 at 60");

typedef enum { IMG_OK = 0, IMG_BAD_MAGIC, IMG_BAD_HDR_VERSION, IMG_BAD_HDR_CRC, IMG_BAD_SIZE,
               IMG_BAD_LOAD_ADDR, IMG_BAD_CRC } img_rc_t;

static img_rc_t img_verify(const uint8_t *slot, uint32_t slot_len, uint32_t load_addr, img_hdr_t *out) {
    img_hdr_t h;
    if (slot_len < IMG_HDR_SIZE) return IMG_BAD_SIZE;
    memcpy(&h, slot, sizeof h);                       /* 리틀 엔디언 호스트·타깃 가정 */
    if (h.magic != IMG_MAGIC) return IMG_BAD_MAGIC;
    if (h.hdr_version != 1u || h.hdr_size != IMG_HDR_SIZE) return IMG_BAD_HDR_VERSION;
    if (crc32_ieee(slot, offsetof(img_hdr_t, hdr_crc32)) != h.hdr_crc32) return IMG_BAD_HDR_CRC;
    if (h.payload_size > slot_len - IMG_HDR_SIZE) return IMG_BAD_SIZE;   /* 뺄셈 쪽: 오버플로 안전 */
    if (h.load_addr != load_addr) return IMG_BAD_LOAD_ADDR;
    if (crc32_ieee(slot + IMG_HDR_SIZE, h.payload_size) != h.payload_crc32) return IMG_BAD_CRC;
    if (out) *out = h;
    return IMG_OK;
}

/* ======================= 레이아웃 (이 레슨의 정의) ======================= */
#define SECTOR_SIZE   256u
#define N_SECT        12u
#define PROG_UNIT     8u
#define PAGE_BYTES    64u          /* 복사할 때 program 한 번의 크기 */
#define SLOT_SECTORS  4u
#define SLOT_BYTES    (SLOT_SECTORS * SECTOR_SIZE)
#define SEC_BOOT      0u           /* 부트로더 (이 레슨에서는 건드리지 않음) */
#define SEC_META0     1u           /* 메타데이터 사본 0 */
#define SEC_META1     2u           /* 메타데이터 사본 1 */
#define SEC_PRI       3u           /* primary 슬롯 3..6 — 여기서 실행 */
#define SEC_SEC       7u           /* secondary 슬롯 7..10 — 새 이미지 내려받는 곳 */
#define SEC_SCRATCH   11u
#define SECTOR_ADDR(s) (FLASH_BASE + (uint32_t)(s) * SECTOR_SIZE)
#define PRI_LOAD_ADDR  (SECTOR_ADDR(SEC_PRI) + IMG_HDR_SIZE)

/* ======================= 메타데이터 (이중 사본) ======================= */
#define META_MAGIC    0x4154454Du  /* "META" */
enum { ST_IDLE = 0x11, ST_SWAP = 0x22, ST_TEST = 0x33 };     /* 0x00/0xFF 를 피한 값 */
enum { DIR_NONE = 0, DIR_UPGRADE = 1, DIR_REVERT = 2 };
#define FL_CONFIRMED  1u
#define FL_REVERTED   2u
#define FL_REJECTED   4u

typedef struct {
    uint32_t magic;
    uint32_t seq;            /* commit 마다 +1, 비교는 wrap-safe */
    uint8_t  state;          /* ST_IDLE / ST_SWAP / ST_TEST */
    uint8_t  dir;            /* ST_SWAP 일 때 DIR_UPGRADE / DIR_REVERT */
    uint8_t  idx;            /* 다음에 할 섹터 (SLOT_SECTORS-1 → 0) */
    uint8_t  step;           /* 그 섹터의 다음 단계 0..2 */
    uint32_t flags;          /* FL_* */
    uint32_t n_swaps;        /* 완료된 스왑 수 (텔레메트리) */
    uint32_t rsv[2];
    uint32_t crc;            /* [0, 28) 의 CRC-32 */
} meta_t;
_Static_assert(sizeof(meta_t) == 32, "meta record = 32 B = 4 program units");

static uint32_t meta_crc(const meta_t *m) { return crc32_ieee(m, offsetof(meta_t, crc)); }

static bool meta_valid(const meta_t *m) {
    if (m->magic != META_MAGIC || (g_bug != BUG_NO_CRC && meta_crc(m) != m->crc)) return false;   /* 연습 3 */
    if (m->state == ST_IDLE || m->state == ST_TEST) return true;
    return m->state == ST_SWAP && (m->dir == DIR_UPGRADE || m->dir == DIR_REVERT) &&
           m->idx < SLOT_SECTORS && m->step < 3u;
}

MAYBE_UNUSED static bool seq_newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }   /* a 가 b 보다 새것? */

int        meta_load(const flash_t *f, meta_t *out);
flash_rc_t meta_commit(flash_t *f, meta_t *m);

/* ---- 연습 4: append-only 로그 메타. 두 섹터 × LOG_SLOTS 칸. 가장 새 유효 레코드가 이긴다.
 * 추가는 "가장 새 레코드가 있는 섹터" 의 마지막 비어 있지 않은 칸 다음 칸에. 칸이 없으면 다른 섹터를 지우고 칸 0.
 * 가장 새 유효 레코드는 절대 덮이거나 지워지지 않는다 (같은 불변식, erase 는 LOG_SLOTS 번에 한 번). */
#define LOG_SLOTS (SECTOR_SIZE / (uint32_t)sizeof(meta_t))
static uint32_t rec_addr(uint32_t sec, uint32_t j) { return SECTOR_ADDR(SEC_META0 + sec) + j * (uint32_t)sizeof(meta_t); }
static int meta_load_log(const flash_t *f, meta_t *out, uint32_t *where) {
    int found = -1;
    for (uint32_t k = 0; k < 2; k++)
        for (uint32_t j = 0; j < LOG_SLOTS; j++) {
            meta_t r;
            flash_read(f, rec_addr(k, j), (uint8_t *)&r, sizeof r);
            if (meta_valid(&r) && (found < 0 || seq_newer(r.seq, out->seq))) {
                *out = r; *where = k * LOG_SLOTS + j; found = (int)k;
            }
        }
    return found;
}
static flash_rc_t meta_commit_log(flash_t *f, meta_t *m) {
    meta_t cur, chk;
    uint32_t where = 0;
    int w = meta_load_log(f, &cur, &where);
    uint32_t sec = w < 0 ? 0u : where / LOG_SLOTS, next = 0;
    for (uint32_t j = 0; j < LOG_SLOTS; j++) {                 /* 마지막으로 뭔가 쓰인 칸 다음 */
        uint8_t b[sizeof(meta_t)];
        flash_read(f, rec_addr(sec, j), b, sizeof b);
        for (uint32_t i = 0; i < sizeof b; i++) if (b[i] != 0xFFu) { next = j + 1u; break; }
    }
    m->magic = META_MAGIC; m->seq = w < 0 ? 1u : cur.seq + 1u; m->rsv[0] = m->rsv[1] = 0; m->crc = meta_crc(m);
    flash_rc_t rc = FLASH_OK;
    if (w < 0 || next >= LOG_SLOTS) {                          /* 꽉 참(또는 없음) → 다른 섹터를 지우고 거기 칸 0 */
        sec = w < 0 ? 0u : 1u - sec; next = 0;
        rc = flash_erase_sector(f, SEC_META0 + sec);
    }
    if (rc == FLASH_OK) rc = flash_program(f, rec_addr(sec, next), (const uint8_t *)m, sizeof *m);
    if (rc != FLASH_OK) return rc;
    flash_read(f, rec_addr(sec, next), (uint8_t *)&chk, sizeof chk);
    return memcmp(&chk, m, sizeof chk) == 0 ? FLASH_OK : FLASH_ERR_VERIFY;
}

/* TODO(A1): 두 사본(SEC_META0, SEC_META1)을 읽어 meta_valid 인 것 중 seq 가 더 새것(seq_newer)을 *out 에.
 * 반환: 고른 사본 번호 0/1, 둘 다 무효면 -1. */
int meta_load(const flash_t *f, meta_t *out) {
    if (g_meta_log) { uint32_t where; return meta_load_log(f, out, &where); }
    meta_t c[2];
    bool v[2];
    for (int k = 0; k < 2; k++) {
        flash_read(f, SECTOR_ADDR(SEC_META0 + (uint32_t)k), (uint8_t *)&c[k], sizeof c[k]);
        v[k] = meta_valid(&c[k]);
    }
    if (!v[0] && !v[1]) return -1;
    int w = !v[1] ? 0 : !v[0] ? 1 : (seq_newer(c[1].seq, c[0].seq) ? 1 : 0);
    *out = c[w];
    return w;
}

/* TODO(A2): 새 레코드 *m 을 "가장 새 유효 사본이 아닌 쪽" 에 쓴다: erase → program(32 B) → read-back 검증.
 * magic, seq(= 최신 + 1, 유효 사본이 없으면 1), crc 는 여기서 채운다 (*m 도 갱신).
 * 가장 새 유효 사본은 절대 건드리지 않는다 — 이게 전원 차단 내성의 전부다. */
flash_rc_t meta_commit(flash_t *f, meta_t *m) {
    if (g_meta_log) return meta_commit_log(f, m);
    meta_t cur, chk;
    int w = meta_load(f, &cur);
    uint32_t target = SEC_META0 + ((w == 0 && g_bug != BUG_SINGLE_COPY) ? 1u : 0u);   /* 연습 2 */
    m->magic = META_MAGIC;
    m->seq = (w < 0) ? 1u : cur.seq + 1u;
    m->rsv[0] = m->rsv[1] = 0;
    m->crc = meta_crc(m);
    flash_rc_t rc = flash_erase_sector(f, target);
    if (rc == FLASH_OK) rc = flash_program(f, SECTOR_ADDR(target), (const uint8_t *)m, sizeof *m);
    if (rc != FLASH_OK) return rc;
    flash_read(f, SECTOR_ADDR(target), (uint8_t *)&chk, sizeof chk);
    return memcmp(&chk, m, sizeof chk) == 0 ? FLASH_OK : FLASH_ERR_VERIFY;
}

/* ======================= 섹터 복사 (주어짐) ======================= */
MAYBE_UNUSED static flash_rc_t copy_sector(flash_t *f, uint32_t dst, uint32_t src) {
    static uint8_t buf[SECTOR_SIZE], chk[SECTOR_SIZE];
    flash_read(f, SECTOR_ADDR(src), buf, SECTOR_SIZE);
    flash_rc_t rc = flash_erase_sector(f, dst);
    for (uint32_t o = 0; rc == FLASH_OK && o < SECTOR_SIZE; o += PAGE_BYTES)
        rc = flash_program(f, SECTOR_ADDR(dst) + o, buf + o, PAGE_BYTES);
    if (rc != FLASH_OK) return rc;
    flash_read(f, SECTOR_ADDR(dst), chk, SECTOR_SIZE);
    return memcmp(buf, chk, SECTOR_SIZE) == 0 ? FLASH_OK : FLASH_ERR_VERIFY;
}

flash_rc_t swap_step(flash_t *f, uint32_t idx, uint32_t step);
flash_rc_t swap_resume(flash_t *f, meta_t *m);

/* TODO(A3): 섹터 idx 의 단계 step 하나 (MCUboot swap-scratch 순서):
 *   0: scratch ← secondary[idx]    1: secondary[idx] ← primary[idx]    2: primary[idx] ← scratch
 * 각 단계는 "원본이 아직 멀쩡하면 몇 번 다시 해도 같은 결과" (멱등) 여야 한다. 잘못된 step → FLASH_ERR_RANGE. */
flash_rc_t swap_step(flash_t *f, uint32_t idx, uint32_t step) {
    uint32_t p = SEC_PRI + idx, s = SEC_SEC + idx;
    switch (step) {
    case 0: return copy_sector(f, SEC_SCRATCH, s);
    case 1: return copy_sector(f, s, p);
    case 2: return copy_sector(f, p, SEC_SCRATCH);
    default: return FLASH_ERR_RANGE;
    }
}

/* ======================= 부트로더 ======================= */
typedef enum { BOOT_OK = 0, BOOT_CUT, BOOT_NO_IMAGE, BOOT_FLASH_ERR } boot_rc_t;
typedef struct { img_hdr_t hdr; uint8_t state; uint32_t seq; } boot_info_t;

static bool g_log;                                    /* -v: 부팅 로그 출력 */
__attribute__((format(printf, 1, 2)))
MAYBE_UNUSED static void bl_log(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
MAYBE_UNUSED static const char *st_name(uint8_t s) { return s == ST_IDLE ? "IDLE" : s == ST_SWAP ? "SWAP" : s == ST_TEST ? "TEST" : "?"; }
MAYBE_UNUSED static const char *dir_name(uint8_t d) { return d == DIR_UPGRADE ? "UPGRADE" : d == DIR_REVERT ? "REVERT" : "-"; }

/* TODO(B1): m 이 가리키는 (idx, step) 부터 스왑을 끝까지 진행한다.
 * 규칙: 단계의 데이터를 "다 쓰고 검증한 뒤" 에 진행 상황을 commit (데이터 먼저, 상태 나중).
 * 다음 위치: step 0→1→2, step 2 뒤엔 idx−1 의 step 0. idx 0 의 step 2 뒤에는 최종 레코드:
 *   DIR_UPGRADE → ST_TEST (flags 0),  DIR_REVERT → ST_IDLE + FL_REVERTED.  둘 다 n_swaps + 1.
 * 플래시 오류(전원 차단 포함)는 그대로 반환. 성공하면 *m = 마지막으로 commit 한 레코드. */
flash_rc_t swap_resume(flash_t *f, meta_t *m) {
    bl_log("[BL] swap %s resume at sector %u step %u\n", dir_name(m->dir), m->idx, m->step);
    for (uint32_t guard = 0; guard < 3u * SLOT_SECTORS; guard++) {
        flash_rc_t rc = g_bug == BUG_STATUS_FIRST ? FLASH_OK : swap_step(f, m->idx, m->step);   /* 연습 1 */
        if (rc != FLASH_OK) return rc;
        meta_t n = *m;
        bool last = false;
        if (m->step < 2u) n.step++;
        else if (m->idx > 0u) { n.idx--; n.step = 0; }
        else {
            last = true;
            n.n_swaps++;
            n.idx = 0; n.step = 0;
            if (m->dir == DIR_UPGRADE) { n.state = ST_TEST; n.dir = DIR_NONE; n.flags = 0; }
            else { n.state = ST_IDLE; n.dir = DIR_NONE; n.flags = FL_REVERTED; }
        }
        rc = meta_commit(f, &n);                                /* 2) 그 다음에 상태 */
        if (rc == FLASH_OK && g_bug == BUG_STATUS_FIRST) rc = swap_step(f, m->idx, m->step);  /* 버그: 상태 먼저 */
        if (rc != FLASH_OK) return rc;
        *m = n;
        if (last) { bl_log("[BL] swap done -> %s seq=%u\n", st_name(m->state), m->seq); return FLASH_OK; }
    }
    return FLASH_ERR_VERIFY;                                    /* 도달 불가 (레코드가 유효하면) */
}

boot_rc_t bl_boot(flash_t *f, boot_info_t *bi);

/* TODO(B2): 부트로더 결정 (전원이 들어올 때마다 실행):
 *   1) meta_load. 유효 사본이 없으면 IDLE 로 간주 (공장 초기 상태).
 *   2) ST_TEST 를 보면 = 시험 부팅한 이미지가 confirm 을 못 했다 → REVERT 스왑 요청을 commit.
 *   3) ST_SWAP: 아무것도 옮기기 전(idx == SLOT_SECTORS-1 && step == 0)이면 secondary 이미지를 img_verify —
 *      무효면 ST_IDLE + FL_REJECTED commit 하고 스왑하지 않는다. 유효하거나 이미 진행 중이면 swap_resume.
 *      (진행 중인 스왑의 secondary 는 반쯤 섞여 있어 검증하면 안 된다!)
 *   4) primary 를 img_verify → OK 면 bi 채우고 BOOT_OK, 아니면 BOOT_NO_IMAGE.
 *   flash 결과가 FLASH_POWER_CUT 이면 BOOT_CUT, 다른 오류면 BOOT_FLASH_ERR. */
boot_rc_t bl_boot(flash_t *f, boot_info_t *bi) {
    static uint8_t slot[SLOT_BYTES];
    meta_t m;
    int w = meta_load(f, &m);
    if (w < 0) { memset(&m, 0, sizeof m); m.state = ST_IDLE; bl_log("[BL] meta: no valid copy -> IDLE\n"); }
    else bl_log("[BL] meta: copy %d seq=%u %s %s idx=%u step=%u flags=%u\n", w, m.seq, st_name(m.state),
                dir_name(m.dir), m.idx, m.step, m.flags);
    flash_rc_t rc = FLASH_OK;
    if (m.state == ST_TEST) {
        bl_log("[BL] TEST image not confirmed -> revert\n");
        m.state = ST_SWAP; m.dir = DIR_REVERT; m.idx = SLOT_SECTORS - 1u; m.step = 0; m.flags = 0;
        rc = meta_commit(f, &m);
    }
    if (rc == FLASH_OK && m.state == ST_SWAP) {
        bool fresh = m.idx == SLOT_SECTORS - 1u && m.step == 0u;
        img_rc_t ir = IMG_OK;
        if (fresh) {
            flash_read(f, SECTOR_ADDR(SEC_SEC), slot, SLOT_BYTES);
            ir = img_verify(slot, SLOT_BYTES, PRI_LOAD_ADDR, NULL);
        }
        if (ir != IMG_OK) {
            bl_log("[BL] secondary invalid (img rc %d) -> reject request\n", (int)ir);
            m.state = ST_IDLE; m.dir = DIR_NONE; m.flags = FL_REJECTED;
            rc = meta_commit(f, &m);
        } else {
            rc = swap_resume(f, &m);
        }
    }
    if (rc == FLASH_POWER_CUT) return BOOT_CUT;
    if (rc != FLASH_OK) return BOOT_FLASH_ERR;
    flash_read(f, SECTOR_ADDR(SEC_PRI), slot, SLOT_BYTES);
    img_hdr_t h;
    img_rc_t ir = img_verify(slot, SLOT_BYTES, PRI_LOAD_ADDR, &h);
    if (ir != IMG_OK) { bl_log("[BL] primary invalid (img rc %d) -> NO IMAGE\n", (int)ir); return BOOT_NO_IMAGE; }
    bi->hdr = h; bi->state = m.state; bi->seq = m.seq;
    bl_log("[BL] jump primary v%u.%u.%u (meta %s)\n", h.img_version >> 24, (h.img_version >> 16) & 0xFFu,
           h.img_version & 0xFFFFu, st_name(m.state));
    return BOOT_OK;
}

/* ======================= 앱 쪽 (스테이징은 주어짐) ======================= */
/* 새 이미지를 secondary 에 쓰고 검증한 "뒤에" 업데이트 요청(ST_SWAP UPGRADE, 마지막 섹터 step 0)을 commit */
static flash_rc_t app_stage_update(flash_t *f, const uint8_t *img) {
    static uint8_t chk[SLOT_BYTES];
    flash_rc_t rc = FLASH_OK;
    for (uint32_t i = 0; rc == FLASH_OK && i < SLOT_SECTORS; i++) rc = flash_erase_sector(f, SEC_SEC + i);
    for (uint32_t o = 0; rc == FLASH_OK && o < SLOT_BYTES; o += PAGE_BYTES)
        rc = flash_program(f, SECTOR_ADDR(SEC_SEC) + o, img + o, PAGE_BYTES);
    if (rc != FLASH_OK) return rc;
    flash_read(f, SECTOR_ADDR(SEC_SEC), chk, SLOT_BYTES);
    if (memcmp(chk, img, SLOT_BYTES) != 0) return FLASH_ERR_VERIFY;
    meta_t cur, m;
    memset(&m, 0, sizeof m);
    if (meta_load(f, &cur) >= 0) m.n_swaps = cur.n_swaps;
    m.state = ST_SWAP; m.dir = DIR_UPGRADE; m.idx = SLOT_SECTORS - 1u; m.step = 0; m.flags = 0;
    return meta_commit(f, &m);
}

flash_rc_t app_confirm(flash_t *f);

/* TODO(C1): 새 이미지가 자가 진단을 통과한 뒤 호출 (MCUboot 의 boot_set_confirmed 에 해당).
 * 현재 상태가 ST_TEST 일 때만 ST_IDLE + FL_CONFIRMED 를 commit. 다른 상태면 아무 op 없이 FLASH_OK. */
flash_rc_t app_confirm(flash_t *f) {
    meta_t m;
    if (meta_load(f, &m) < 0 || m.state != ST_TEST) return FLASH_OK;
    m.state = ST_IDLE; m.dir = DIR_NONE; m.flags = FL_CONFIRMED;
    return meta_commit(f, &m);
}

/* ======================= Part C: 비용 ======================= */
typedef struct {
    uint32_t erase_us[MAX_SECTORS];   /* 섹터별 erase 시간 (섹터 크기가 다를 수 있음) */
    uint32_t prog_unit_bytes;         /* program 단위 (F4 x32: 4 B) */
    uint32_t prog_unit_us;            /* 단위당 시간 */
    uint32_t endurance;               /* 섹터당 erase 수명 */
} flash_timing_t;
typedef struct {
    uint32_t erases;                  /* 총 erase 수 */
    uint64_t prog_units;              /* 총 program 단위 수 (섹터별 올림 합) */
    uint64_t time_us;                 /* erase + program 시간 합 */
    uint32_t hot_sector;              /* erase 가 가장 많은 섹터 (동률이면 번호 작은 쪽) */
    uint32_t hot_erases;
    uint32_t updates_to_wearout;      /* endurance / hot_erases (hot_erases == 0 → UINT32_MAX) */
} cost_t;

void cost_estimate(const uint32_t *erase_cnt, const uint64_t *prog_bytes, uint32_t n,
                   const flash_timing_t *t, cost_t *out);

/* TODO(C2): 업데이트 한 번의 섹터별 erase 횟수·program 바이트로 시간과 수명을 추정한다 (cost_t 주석 참고). */
void cost_estimate(const uint32_t *erase_cnt, const uint64_t *prog_bytes, uint32_t n,
                   const flash_timing_t *t, cost_t *out) {
    memset(out, 0, sizeof *out);
    for (uint32_t s = 0; s < n; s++) {
        uint64_t units = (prog_bytes[s] + t->prog_unit_bytes - 1u) / t->prog_unit_bytes;
        out->erases += erase_cnt[s];
        out->prog_units += units;
        out->time_us += (uint64_t)erase_cnt[s] * t->erase_us[s] + units * t->prog_unit_us;
        if (erase_cnt[s] > out->hot_erases) { out->hot_erases = erase_cnt[s]; out->hot_sector = s; }
    }
    out->updates_to_wearout = out->hot_erases ? t->endurance / out->hot_erases : UINT32_MAX;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static flash_t g_f;
static uint8_t g_imgA[SLOT_BYTES], g_imgB[SLOT_BYTES];
static uint8_t g_factory[FLASH_MAX];

static void img_make(uint8_t *slot, uint32_t version, uint32_t payload, uint32_t seed) {
    img_hdr_t h;
    memset(slot, 0xFF, SLOT_BYTES);
    for (uint32_t i = 0; i < payload; i++) slot[IMG_HDR_SIZE + i] = (uint8_t)xs32(&seed);
    memset(&h, 0, sizeof h);
    h.magic = IMG_MAGIC; h.hdr_version = 1; h.hdr_size = IMG_HDR_SIZE; h.img_version = version;
    h.payload_size = payload; h.load_addr = PRI_LOAD_ADDR; h.flags = 0;
    h.payload_crc32 = crc32_ieee(slot + IMG_HDR_SIZE, payload);
    h.hdr_crc32 = crc32_ieee(&h, offsetof(img_hdr_t, hdr_crc32));
    memcpy(slot, &h, sizeof h);
}

/* 공장 상태: primary = A(v1.2.0), 메타 사본0 = IDLE+CONFIRMED seq 1, 나머지 erased. op 카운터 0. */
static void factory_build(void) {
    img_make(g_imgA, 0x01020000u, 600, 0xA5A5A5A5u);
    img_make(g_imgB, 0x01030000u, 900, 0x5A5A5A5Au);
    flash_init(&g_f, SECTOR_SIZE, N_SECT, PROG_UNIT);
    memcpy(&g_f.mem[SEC_PRI * SECTOR_SIZE], g_imgA, SLOT_BYTES);
    meta_t m;
    memset(&m, 0, sizeof m);
    m.magic = META_MAGIC; m.seq = 1; m.state = ST_IDLE; m.flags = FL_CONFIRMED;
    m.crc = meta_crc(&m);
    memcpy(&g_f.mem[SEC_META0 * SECTOR_SIZE], &m, sizeof m);
    memcpy(g_factory, g_f.mem, FLASH_MAX);
}
static void factory_reset(uint32_t seed) {
    flash_init(&g_f, SECTOR_SIZE, N_SECT, PROG_UNIT);
    memcpy(g_f.mem, g_factory, FLASH_MAX);
    g_f.rng = seed ? seed : 1u;
}

static int which_image(void) {                        /* primary 가 A(1)/B(2)/둘 다 아님(0) */
    static uint8_t buf[SLOT_BYTES];
    flash_read(&g_f, SECTOR_ADDR(SEC_PRI), buf, SLOT_BYTES);
    if (img_verify(buf, SLOT_BYTES, PRI_LOAD_ADDR, NULL) != IMG_OK) return 0;
    return memcmp(buf, g_imgA, SLOT_BYTES) == 0 ? 1 : memcmp(buf, g_imgB, SLOT_BYTES) == 0 ? 2 : 0;
}

/* ---------- 수명주기 시뮬레이션: 스테이징 → 재부팅 → 스왑 → 시험 부팅 → confirm/안 함 → 재부팅 ... ---------- */
enum { SC_CONFIRM = 0, SC_NOCONFIRM = 1 };
enum { OUT_NONE = 0, OUT_NEW, OUT_OLD_REVERTED };
enum { V_NONE = 0, V_BOOT_FAIL, V_FOREIGN, V_FLASH_ERR, V_STATE, V_NO_END, V_OUTCOME, V_COUNT };
static const char *const V_NAME[V_COUNT] = { "-", "boot-fail(no valid image)", "foreign image", "flash error",
                                              "unexpected state", "no termination", "wrong outcome" };
#define MAX_EVENTS 24

typedef struct {
    const uint32_t *cuts;    /* 차단 계획 */
    uint32_t n_cuts, next;
    bool relative;           /* true: 전원 켤 때 op_index + cuts[i] 에 건다 */
    int scenario, out, viol;
    uint32_t boots, cuts_hit, ops, boots_new, boots_old;
    bool cut_in_trial;      /* 차단 직후 가장 새 메타가 ST_TEST 였다 = 시험 부팅 창에서 끊김 */
    uint32_t op_stage_end, op_swap_end, op_confirm_end;   /* 차단 없는 실행의 단계 경계 (op 지도) */
} life_t;

static void arm_next_cut(life_t *L) {
    if (L->next >= L->n_cuts) return;
    uint32_t c = L->cuts[L->next++];
    g_f.cut_at = L->relative ? g_f.op_index + c : (c > g_f.op_index ? c : 0u);
}
static void power_cycle(life_t *L) {
    if (g_log) printf("[PWR] power cut at op %u -> reboot\n", g_f.op_index);
    flash_power_on(&g_f);
    meta_t m;
    if (meta_load(&g_f, &m) >= 0 && m.state == ST_TEST) L->cut_in_trial = true;
    L->cuts_hit++;
    arm_next_cut(L);
}

static void life_run(int scenario, const uint32_t *cuts, uint32_t n_cuts, bool relative, uint32_t mode,
                     uint32_t seed, life_t *L) {
    memset(L, 0, sizeof *L);
    L->cuts = cuts; L->n_cuts = n_cuts; L->relative = relative; L->scenario = scenario;
    factory_reset(seed);
    g_f.cut_mode = mode;
    arm_next_cut(L);
    for (int ev = 0; ev < MAX_EVENTS && L->out == OUT_NONE && L->viol == V_NONE; ev++) {
        boot_info_t bi;
        boot_rc_t br = bl_boot(&g_f, &bi);
        if (br == BOOT_CUT) { power_cycle(L); continue; }
        if (br != BOOT_OK) { L->viol = br == BOOT_FLASH_ERR ? V_FLASH_ERR : V_BOOT_FAIL; break; }
        L->boots++;
        if (!L->op_swap_end && bi.state == ST_TEST) L->op_swap_end = g_f.op_index;
        int img = which_image();                      /* 부트로더와 독립적인 확인 */
        if (img == 0) { L->viol = V_FOREIGN; break; }
        if (img == 1) L->boots_old++; else L->boots_new++;
        meta_t m;
        if (meta_load(&g_f, &m) < 0) { L->viol = V_STATE; break; }
        if (img == 1 && m.state == ST_IDLE && !(m.flags & (FL_REVERTED | FL_REJECTED))) {
            if (g_log) printf("[APP] v1.2.0 running, staging v1.3.0 into secondary\n");
            flash_rc_t rc = app_stage_update(&g_f, g_imgB);
            if (rc == FLASH_POWER_CUT) { power_cycle(L); continue; }
            if (rc != FLASH_OK) { L->viol = V_FLASH_ERR; break; }
            if (!L->op_stage_end) L->op_stage_end = g_f.op_index;
            if (g_log) printf("[APP] update requested -> reset\n");
            continue;
        }
        if (img == 2 && m.state == ST_TEST) {
            if (scenario == SC_CONFIRM) {
                flash_rc_t rc = app_confirm(&g_f);
                if (rc == FLASH_POWER_CUT) { power_cycle(L); continue; }
                if (rc != FLASH_OK) { L->viol = V_FLASH_ERR; break; }
                if (!L->op_confirm_end) L->op_confirm_end = g_f.op_index;
                if (g_log) printf("[APP] v1.3.0 self-test OK -> confirmed\n");
            } else if (g_log) printf("[APP] v1.3.0 self-test FAILED -> watchdog reset (no confirm)\n");
            continue;
        }
        if (img == 2 && m.state == ST_IDLE && (m.flags & FL_CONFIRMED)) L->out = OUT_NEW;
        else if (img == 1 && m.state == ST_IDLE && (m.flags & FL_REVERTED)) L->out = OUT_OLD_REVERTED;
        else L->viol = V_STATE;
    }
    if (L->out == OUT_NONE && L->viol == V_NONE) L->viol = V_NO_END;
    if (L->viol == V_NONE) {
        bool ok = scenario == SC_NOCONFIRM ? L->out == OUT_OLD_REVERTED
                                           : (L->out == OUT_NEW || (L->out == OUT_OLD_REVERTED && L->cut_in_trial));
        if (!ok) L->viol = V_OUTCOME;
    }
    L->ops = g_f.op_index;
}

/* 집계 */
typedef struct {
    uint32_t scenarios, viol, viol_kind[V_COUNT], boots, out_new, out_old, max_boots, cut_erase, cut_prog;
    uint32_t first_bad_cut, first_bad_kind, trial_reverts;
} agg_t;
static void agg_add(agg_t *a, const life_t *L, uint32_t cut_for_report) {
    a->scenarios++;
    a->boots += L->boots;
    if (L->boots > a->max_boots) a->max_boots = L->boots;
    if (L->out == OUT_NEW) a->out_new++;
    if (L->out == OUT_OLD_REVERTED) a->out_old++;
    if (L->scenario == SC_CONFIRM && L->out == OUT_OLD_REVERTED) a->trial_reverts++;
    if (L->viol != V_NONE) {
        if (a->viol == 0) { a->first_bad_cut = cut_for_report; a->first_bad_kind = (uint32_t)L->viol; }
        a->viol++; a->viol_kind[L->viol]++;
    }
}

static uint32_t g_T[2];                               /* 차단 없는 수명주기의 총 op 수 (시나리오별) */

/* ======================= 추가 연습 ======================= */
/* 모드 마스크: bit0 BEFORE, bit1 AFTER, bit2 TORN(시드 4개) */
static agg_t sweep_single(unsigned mode_mask) {
    agg_t a;
    memset(&a, 0, sizeof a);
    for (int sc = 0; sc < 2; sc++)
        for (uint32_t k = 1; k <= g_T[sc]; k++)
            for (uint32_t v = 0; v < 6; v++) {
                if (!(mode_mask & (v == 0 ? 1u : v == 1 ? 2u : 4u))) continue;
                uint32_t mode = v == 0 ? CUT_BEFORE : v == 1 ? CUT_AFTER : CUT_TORN;
                life_t L;
                life_run(sc, &k, 1, false, mode, 0x9E3779B9u * (v + 1u) + k, &L);
                agg_add(&a, &L, k);
            }
    return a;
}
static agg_t sweep_double(void) {
    agg_t a;
    memset(&a, 0, sizeof a);
    for (int sc = 0; sc < 2; sc++)
        for (uint32_t k1 = 1; k1 <= g_T[sc]; k1++) {
            life_t L;
            life_run(sc, &k1, 1, false, CUT_TORN, k1 * 7919u, &L);
            uint32_t end = L.ops;
            for (uint32_t k2 = k1 + 1; k2 <= end; k2++) {
                uint32_t cuts[2] = { k1, k2 };
                life_run(sc, cuts, 2, false, CUT_TORN, k1 * 7919u + k2, &L);
                agg_add(&a, &L, k1 * 1000u + k2);
            }
        }
    return a;
}
static void measure_T(void) {
    life_t L;
    life_run(SC_CONFIRM, NULL, 0, false, CUT_TORN, 1, &L);
    g_T[SC_CONFIRM] = L.ops;
    life_run(SC_NOCONFIRM, NULL, 0, false, CUT_TORN, 1, &L);
    g_T[SC_NOCONFIRM] = L.ops;
}

/* 연습 1~3: 그럴듯한 버그 세 개를 넣고 전수 차단으로 잡는다 */
static void ex_mutants(void) {
    static const char *const name[4] = { "none", "status-before-data", "single-meta-copy", "meta-without-CRC" };
    for (int b = BUG_STATUS_FIRST; b <= BUG_NO_CRC; b++) {
        g_bug = b;
        measure_T();
        agg_t all = sweep_single(7u), clean = sweep_single(3u), torn = sweep_single(4u);
        printf("ex%d %-18s: single-cut violations all=%u (before/after=%u, torn=%u) first: cut %u %s\n", b, name[b],
               all.viol, clean.viol, torn.viol, all.first_bad_cut, V_NAME[all.first_bad_kind]);
        CHECK(all.viol > 0u);
        if (b == BUG_STATUS_FIRST) CHECK(clean.viol > 0u);      /* 찢김이 없어도(깨끗한 차단만으로) 깨진다 */
        if (b == BUG_NO_CRC) CHECK(clean.viol == 0u && torn.viol > 0u);   /* 찢긴 op 만 이 버그를 드러낸다 */
    }
    g_bug = BUG_NONE;
}

/* 연습 4: append-only 로그 메타 (FTL 식) — 같은 보장, erase 는 훨씬 적게 */
static void ex_meta_log(void) {
    uint32_t er[2][MAX_SECTORS];
    for (int lg = 0; lg < 2; lg++) {
        g_meta_log = lg != 0;
        life_t L;
        life_run(SC_CONFIRM, NULL, 0, false, CUT_TORN, 1, &L);
        CHECK(L.viol == V_NONE && L.out == OUT_NEW);
        for (uint32_t s = 0; s < MAX_SECTORS; s++) er[lg][s] = g_f.erase_cnt[s];
    }
    uint32_t m0 = er[0][SEC_META0] + er[0][SEC_META1], m1 = er[1][SEC_META0] + er[1][SEC_META1];
    printf("ex4 meta erases per confirmed update: dual-copy=%u log(%u slots/sector)=%u (sector1 %u, sector2 %u), scratch=%u\n",
           m0, (unsigned)LOG_SLOTS, m1, er[1][SEC_META0], er[1][SEC_META1], er[1][SEC_SCRATCH]);
    CHECK(m0 == 14u && m1 < m0 / 4u);
    CHECK(er[1][SEC_META0] < er[1][SEC_SCRATCH] && er[1][SEC_META1] < er[1][SEC_SCRATCH]);  /* 이제 scratch 가 가장 뜨겁다 */
    g_meta_log = true;
    measure_T();
    agg_t a = sweep_single(7u), d = sweep_double();
    printf("ex4 log meta: single scenarios=%u violations=%u, double scenarios=%u violations=%u\n",
           a.scenarios, a.viol, d.scenarios, d.viol);
    CHECK(a.scenarios == 6u * (g_T[0] + g_T[1]) && a.viol == 0u);
    CHECK(d.scenarios > 10000u && d.viol == 0u);
    g_meta_log = false;
}

/* 연습 5: scratch 수명 — MCUboot 문서의 식과 이 레슨의 F4 급 숫자 */
static uint32_t mcuboot_upgrades(uint32_t cycles, uint32_t image_kb, uint32_t scratch_kb) {
    return (uint32_t)((uint64_t)cycles * scratch_kb / image_kb);   /* cycles / (image/scratch), 내림 */
}
static void ex_scratch_life(void) {
    uint32_t a = mcuboot_upgrades(10000, 150, 4), b = mcuboot_upgrades(10000, 150, 16);
    uint32_t f4 = mcuboot_upgrades(10000, 512, 128);
    printf("ex5 scratch life: 150K image/4K scratch=%u, /16K=%u, F4 512K image/128K scratch=%u updates"
           " (dual-copy meta limits to %u)\n", a, b, f4, 10000u / 7u);
    CHECK(a == 266u && b == 1066u && f4 == 2500u);
    CHECK(10000u / 7u < f4);                                          /* 본문 설계의 수명은 메타가 정한다 */
}

int main(void) {
    factory_build();
    ex_mutants();
    ex_meta_log();
    ex_scratch_life();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
