/* BM-A06 전원 차단 내성 업데이트: 모든 쓰기 지점 power-cut 주입
 *
 * 설계 (b): 메타데이터 이중 사본(시퀀스 번호 + CRC-32, "유효한 것 중 가장 새것이 이긴다") 이
 *           MCUboot 식 scratch 스왑(섹터마다 3 단계, 큰 섹터 인덱스부터)을 구동한다.
 * Part A — 메타데이터 load/commit (이중 사본), 스왑 한 단계 (멱등)
 * Part B — 재개(resume) 상태 기계 + 부트로더 결정, 모든 플래시 op 에서 전원 차단 전수 주입
 *          (단일 차단 × 3 모드 × 시드, 이중 차단 전수, 무작위 다중 차단 "폭풍")
 * Part C — test/confirm/revert 수명주기, 업데이트 비용(섹터별 erase·시간·수명), 부팅 로그
 *
 * 플래시 시간·수명 숫자는 대표값(데이터시트 확인 권장). 테스트의 정답은 이 파일의 모델 정의다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so     (-v: 부팅 로그 + op 지도)
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAYBE_UNUSED __attribute__((unused))   /* 시작 코드(스텁)에서 아직 안 쓰는 도우미의 경고 끄기 */

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
    if (m->magic != META_MAGIC || meta_crc(m) != m->crc) return false;
    if (m->state == ST_IDLE || m->state == ST_TEST) return true;
    return m->state == ST_SWAP && (m->dir == DIR_UPGRADE || m->dir == DIR_REVERT) &&
           m->idx < SLOT_SECTORS && m->step < 3u;
}

MAYBE_UNUSED static bool seq_newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }   /* a 가 b 보다 새것? */

int        meta_load(const flash_t *f, meta_t *out);
flash_rc_t meta_commit(flash_t *f, meta_t *m);

/* TODO(A1): 두 사본(SEC_META0, SEC_META1)을 읽어 meta_valid 인 것 중 seq 가 더 새것(seq_newer)을 *out 에.
 * 반환: 고른 사본 번호 0/1, 둘 다 무효면 -1. */
int meta_load(const flash_t *f, meta_t *out) {
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
    meta_t cur, chk;
    int w = meta_load(f, &cur);
    uint32_t target = SEC_META0 + (w == 0 ? 1u : 0u);
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
        flash_rc_t rc = swap_step(f, m->idx, m->step);         /* 1) 데이터 */
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
static bool g_verbose;
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
static void agg_print(const char *name, const agg_t *a) {
    printf("%s: scenarios=%u boots=%u (max %u/scenario) new=%u reverted=%u (confirm-scenario reverts %u) violations=%u",
           name, a->scenarios, a->boots, a->max_boots, a->out_new, a->out_old, a->trial_reverts, a->viol);
    if (a->viol) printf(" first: cut %u %s", a->first_bad_cut, V_NAME[a->first_bad_kind]);
    printf("\n");
}

static uint32_t g_T[2];                               /* 차단 없는 수명주기의 총 op 수 (시나리오별) */

/* ---------- Part A ---------- */
static void test_part_a(void) {
    CHECK(crc32_ieee("123456789", 9) == 0xCBF43926u);
    factory_build();
    img_hdr_t h;
    CHECK(img_verify(g_imgA, SLOT_BYTES, PRI_LOAD_ADDR, &h) == IMG_OK && h.img_version == 0x01020000u);
    CHECK(img_verify(g_imgB, SLOT_BYTES, PRI_LOAD_ADDR, &h) == IMG_OK && h.payload_size == 900u);
    {   /* 헤더 축약판의 거절 코드 */
        static uint8_t t[SLOT_BYTES];
        img_hdr_t x;
        memcpy(t, g_imgB, SLOT_BYTES); t[0] ^= 1u;
        CHECK(img_verify(t, SLOT_BYTES, PRI_LOAD_ADDR, NULL) == IMG_BAD_MAGIC);
        memcpy(t, g_imgB, SLOT_BYTES); t[4] = 2;
        CHECK(img_verify(t, SLOT_BYTES, PRI_LOAD_ADDR, NULL) == IMG_BAD_HDR_VERSION);
        memcpy(t, g_imgB, SLOT_BYTES); t[12] ^= 0x10u;
        CHECK(img_verify(t, SLOT_BYTES, PRI_LOAD_ADDR, NULL) == IMG_BAD_HDR_CRC);
        memcpy(t, g_imgB, SLOT_BYTES); t[IMG_HDR_SIZE + 899] ^= 0x80u;
        CHECK(img_verify(t, SLOT_BYTES, PRI_LOAD_ADDR, NULL) == IMG_BAD_CRC);
        memcpy(&x, g_imgB, sizeof x); x.payload_size = 0xFFFFFFF0u;           /* 오버플로 유도 크기 */
        x.hdr_crc32 = crc32_ieee(&x, offsetof(img_hdr_t, hdr_crc32));
        memcpy(t, g_imgB, SLOT_BYTES); memcpy(t, &x, sizeof x);
        CHECK(img_verify(t, SLOT_BYTES, PRI_LOAD_ADDR, NULL) == IMG_BAD_SIZE);
        memcpy(&x, g_imgB, sizeof x); x.load_addr = SECTOR_ADDR(SEC_SEC) + IMG_HDR_SIZE;
        x.hdr_crc32 = crc32_ieee(&x, offsetof(img_hdr_t, hdr_crc32));
        memcpy(t, g_imgB, SLOT_BYTES); memcpy(t, &x, sizeof x);
        CHECK(img_verify(t, SLOT_BYTES, PRI_LOAD_ADDR, NULL) == IMG_BAD_LOAD_ADDR);
    }

    /* A1/A2: 공장 메타 → 사본 0, seq 1 */
    factory_reset(1);
    meta_t m, n;
    CHECK(meta_load(&g_f, &m) == 0 && m.seq == 1u && m.state == ST_IDLE && m.flags == FL_CONFIRMED);
    /* commit 5 번: 사본이 1,0,1,0,1 로 번갈아, seq 2..6, 직전 최신 사본은 바이트 하나 안 바뀜 */
    int alt_ok = 1;
    for (uint32_t i = 0; i < 5; i++) {
        uint8_t keep[SECTOR_SIZE];
        int w = meta_load(&g_f, &m);
        memcpy(keep, &g_f.mem[(SEC_META0 + (uint32_t)(w < 0 ? 0 : w)) * SECTOR_SIZE], SECTOR_SIZE);
        memset(&n, 0, sizeof n); n.state = ST_IDLE; n.flags = FL_CONFIRMED; n.n_swaps = i;
        flash_rc_t rc = meta_commit(&g_f, &n);
        int w2 = meta_load(&g_f, &m);
        alt_ok &= rc == FLASH_OK && w2 == (int)((i + 1u) & 1u) && m.seq == i + 2u && m.n_swaps == i &&
                  n.seq == m.seq && w >= 0 &&
                  memcmp(keep, &g_f.mem[(SEC_META0 + (uint32_t)(w < 0 ? 0 : w)) * SECTOR_SIZE], SECTOR_SIZE) == 0;
    }
    CHECK(alt_ok);
    CHECK(g_f.erase_cnt[SEC_META0] == 2u && g_f.erase_cnt[SEC_META1] == 3u);

    /* seq wrap: 0xFFFFFFFF 다음은 0 — 0 이 더 새것 */
    {
        meta_t a, b;
        memset(&a, 0, sizeof a); a.magic = META_MAGIC; a.state = ST_IDLE; a.seq = 0xFFFFFFFFu; a.crc = meta_crc(&a);
        b = a; b.seq = 0; b.flags = FL_REVERTED; b.crc = meta_crc(&b);
        memset(&g_f.mem[SEC_META0 * SECTOR_SIZE], 0xFF, 2u * SECTOR_SIZE);
        memcpy(&g_f.mem[SEC_META0 * SECTOR_SIZE], &a, sizeof a);
        memcpy(&g_f.mem[SEC_META1 * SECTOR_SIZE], &b, sizeof b);
        CHECK(meta_load(&g_f, &m) == 1 && m.seq == 0u && m.flags == FL_REVERTED);
        memcpy(&g_f.mem[SEC_META0 * SECTOR_SIZE], &b, sizeof b);
        memcpy(&g_f.mem[SEC_META1 * SECTOR_SIZE], &a, sizeof a);
        CHECK(meta_load(&g_f, &m) == 0 && m.seq == 0u);
        memset(&n, 0, sizeof n); n.state = ST_IDLE;
        CHECK(meta_commit(&g_f, &n) == FLASH_OK && n.seq == 1u && meta_load(&g_f, &m) == 1 && m.seq == 1u);
        /* 최신 사본의 비트 하나가 깨지면(CRC) 옛 사본이 이긴다 */
        g_f.mem[SEC_META1 * SECTOR_SIZE + 9] ^= 0x04u;
        CHECK(meta_load(&g_f, &m) == 0 && m.seq == 0u);
        /* magic 은 맞지만 CRC 가 틀린 쓰레기도 거절 */
        g_f.mem[SEC_META0 * SECTOR_SIZE + 31] ^= 0x01u;
        CHECK(meta_load(&g_f, &m) == -1);
        memset(&g_f.mem[SEC_META0 * SECTOR_SIZE], 0xFF, 2u * SECTOR_SIZE);
        CHECK(meta_load(&g_f, &m) == -1);
        memset(&n, 0, sizeof n); n.state = ST_IDLE;
        CHECK(meta_commit(&g_f, &n) == FLASH_OK && n.seq == 1u && meta_load(&g_f, &m) == 0);
    }

    /* A3: 스왑 단계 — 결과와 멱등성 (같은 단계를 두 번 해도 플래시 내용 동일) */
    factory_reset(1);
    memcpy(&g_f.mem[SEC_SEC * SECTOR_SIZE], g_imgB, SLOT_BYTES);
    static uint8_t snap[FLASH_MAX];
    int idem = 1;
    for (uint32_t step = 0; step < 3; step++) {
        flash_rc_t r1 = swap_step(&g_f, 3, step);
        memcpy(snap, g_f.mem, FLASH_MAX);
        flash_rc_t r2 = swap_step(&g_f, 3, step);
        idem &= r1 == FLASH_OK && r2 == FLASH_OK && memcmp(snap, g_f.mem, FLASH_MAX) == 0;
    }
    CHECK(idem);
    CHECK(memcmp(&g_f.mem[(SEC_PRI + 3) * SECTOR_SIZE], g_imgB + 3 * SECTOR_SIZE, SECTOR_SIZE) == 0 &&
          memcmp(&g_f.mem[(SEC_SEC + 3) * SECTOR_SIZE], g_imgA + 3 * SECTOR_SIZE, SECTOR_SIZE) == 0 &&
          memcmp(&g_f.mem[SEC_SCRATCH * SECTOR_SIZE], g_imgB + 3 * SECTOR_SIZE, SECTOR_SIZE) == 0);
    CHECK(swap_step(&g_f, 0, 3) == FLASH_ERR_RANGE);
    CHECK(g_f.erase_cnt[SEC_SCRATCH] == 2u && g_f.erase_cnt[SEC_PRI + 3] == 2u && g_f.n_prog == 24u);

    /* 차단 없는 업그레이드: 스테이징 → 부팅 → primary = B, secondary = A, 메타 TEST */
    factory_reset(1);
    boot_info_t bi;
    CHECK(app_stage_update(&g_f, g_imgB) == FLASH_OK && meta_load(&g_f, &m) >= 0 && m.state == ST_SWAP &&
          m.dir == DIR_UPGRADE && m.idx == SLOT_SECTORS - 1u && m.step == 0u);
    CHECK(bl_boot(&g_f, &bi) == BOOT_OK && bi.hdr.img_version == 0x01030000u && bi.state == ST_TEST);
    CHECK(memcmp(&g_f.mem[SEC_PRI * SECTOR_SIZE], g_imgB, SLOT_BYTES) == 0 &&
          memcmp(&g_f.mem[SEC_SEC * SECTOR_SIZE], g_imgA, SLOT_BYTES) == 0);
    CHECK(meta_load(&g_f, &m) >= 0 && m.state == ST_TEST && m.n_swaps == 1u && m.seq == 2u + 3u * SLOT_SECTORS);
    uint32_t before = g_f.op_index;
    CHECK(app_confirm(&g_f) == FLASH_OK && meta_load(&g_f, &m) >= 0 && m.state == ST_IDLE &&
          m.flags == FL_CONFIRMED && g_f.op_index == before + 2u);
    before = g_f.op_index;
    CHECK(app_confirm(&g_f) == FLASH_OK && g_f.op_index == before);          /* 이미 확정 → op 없음 */

    /* 차단 없는 수명주기 두 개 → 총 op 수 */
    life_t L;
    life_run(SC_CONFIRM, NULL, 0, false, CUT_TORN, 1, &L);
    CHECK(L.viol == V_NONE && L.out == OUT_NEW && L.boots == 3u);
    g_T[SC_CONFIRM] = L.ops;
    if (g_verbose)
        printf("op map (confirm): stage 1..%u | swap %u..%u | confirm %u..%u\n", L.op_stage_end,
               L.op_stage_end + 1u, L.op_swap_end, L.op_swap_end + 1u, L.op_confirm_end);
    life_run(SC_NOCONFIRM, NULL, 0, false, CUT_TORN, 1, &L);
    CHECK(L.viol == V_NONE && L.out == OUT_OLD_REVERTED && L.boots == 3u && L.boots_new == 1u);
    g_T[SC_NOCONFIRM] = L.ops;
    printf("uncut lifecycle ops: confirm=%u no-confirm(revert)=%u\n", g_T[SC_CONFIRM], g_T[SC_NOCONFIRM]);
}

/* ---------- Part B ---------- */
static void test_part_b(void) {
    meta_t m;
    boot_info_t bi;
    /* B: 검증 실패한 secondary → 요청 거절, primary 는 한 번도 안 지운다 */
    factory_reset(1);
    CHECK(app_stage_update(&g_f, g_imgB) == FLASH_OK);
    g_f.mem[(SEC_SEC + 2) * SECTOR_SIZE + 17] ^= 0x20u;                      /* 다운로드 후 비트 썩음 */
    CHECK(bl_boot(&g_f, &bi) == BOOT_OK && bi.hdr.img_version == 0x01020000u);
    CHECK(meta_load(&g_f, &m) >= 0 && m.state == ST_IDLE && m.flags == FL_REJECTED);
    CHECK(g_f.erase_cnt[SEC_PRI] + g_f.erase_cnt[SEC_PRI + 1] + g_f.erase_cnt[SEC_PRI + 2] +
          g_f.erase_cnt[SEC_PRI + 3] + g_f.erase_cnt[SEC_SCRATCH] == 0u && which_image() == 1);

    /* B: 진행 중(idx 3, step 2) 스왑은 secondary 가 섞여 있어도 검증 없이 재개해야 한다 */
    factory_reset(1);
    CHECK(app_stage_update(&g_f, g_imgB) == FLASH_OK);
    meta_t mid;
    CHECK(meta_load(&g_f, &mid) >= 0);
    CHECK(swap_step(&g_f, 3, 0) == FLASH_OK && swap_step(&g_f, 3, 1) == FLASH_OK);
    mid.idx = 3; mid.step = 2;
    CHECK(meta_commit(&g_f, &mid) == FLASH_OK);
    CHECK(bl_boot(&g_f, &bi) == BOOT_OK && which_image() == 2 && bi.state == ST_TEST);

    /* B: erase 없이 덮어쓰면 AND → 레코드가 깨져 무효 (그래서 commit 은 항상 erase 부터) */
    factory_reset(1);
    memset(&m, 0, sizeof m);
    m.magic = META_MAGIC; m.seq = 7; m.state = ST_TEST; m.crc = meta_crc(&m);
    CHECK(flash_program(&g_f, SECTOR_ADDR(SEC_META0), (const uint8_t *)&m, sizeof m) == FLASH_OK);
    meta_t rb;
    flash_read(&g_f, SECTOR_ADDR(SEC_META0), (uint8_t *)&rb, sizeof rb);
    CHECK(memcmp(&rb, &m, sizeof m) != 0 && !meta_valid(&rb));

    /* B: commit 하나를 op 1(erase)/2(program) 에서 3 모드 × 16 시드로 끊는다 → 옛 상태 또는 새 상태, 그 외 없음 */
    {
        uint32_t ok = 0, total = 0, saw_old = 0, saw_new = 0;
        for (uint32_t mode = 0; mode < 3; mode++)
            for (uint32_t seed = 1; seed <= 16; seed++)
                for (uint32_t cut = 1; cut <= 2; cut++) {
                    factory_reset(seed * 2654435761u);
                    meta_t y;
                    memset(&y, 0, sizeof y); y.state = ST_TEST; y.n_swaps = 9;
                    g_f.cut_mode = mode; g_f.cut_at = cut;
                    flash_rc_t rc = meta_commit(&g_f, &y);
                    flash_power_on(&g_f);
                    int w = meta_load(&g_f, &m);
                    bool is_old = w == 0 && m.seq == 1u && m.state == ST_IDLE;
                    bool is_new = w == 1 && m.seq == 2u && m.state == ST_TEST && m.n_swaps == 9u;
                    total++;
                    ok += rc == FLASH_POWER_CUT && (is_old || is_new);
                    saw_old += is_old; saw_new += is_new;
                }
        CHECK(ok == total);
        CHECK(saw_old > 0 && saw_new > 0);
        printf("torn meta commit: %u cuts -> old state %u, new state %u, other %u\n", total, saw_old, saw_new,
               total - saw_old - saw_new);
    }

    /* B: 모든 op 에서 단일 차단 — 모드 BEFORE / AFTER / TORN×4 시드, 시나리오 2 개 */
    {
        agg_t a;
        memset(&a, 0, sizeof a);
        uint32_t tags = 0;
        for (int sc = 0; sc < 2; sc++)
            for (uint32_t k = 1; k <= g_T[sc]; k++)
                for (uint32_t v = 0; v < 6; v++) {
                    uint32_t mode = v == 0 ? CUT_BEFORE : v == 1 ? CUT_AFTER : CUT_TORN;
                    life_t L;
                    life_run(sc, &k, 1, false, mode, 0x9E3779B9u * (v + 1u) + k, &L);
                    agg_add(&a, &L, k);
                    if (v == 0) {                                   /* BEFORE: trace[k-1] = 끊긴 op 의 종류 */
                        uint32_t kind = k <= TRACE_MAX ? (uint32_t)(g_f.trace[k - 1] >> 8) : 0u;
                        a.cut_erase += kind == OP_ERASE; a.cut_prog += kind == OP_PROG; tags++;
                    }
                }
        agg_print("single cut x {before,after,torn x4}", &a);
        printf("  cut points: N=%u+%u ops (erase %u, program %u)\n", g_T[0], g_T[1], a.cut_erase, a.cut_prog);
        CHECK(a.scenarios == 6u * (g_T[0] + g_T[1]) && tags == g_T[0] + g_T[1]);
        CHECK(a.viol == 0u);
        CHECK(a.cut_erase > 0u && a.cut_prog > 0u && a.cut_erase + a.cut_prog == tags);
        CHECK(a.out_new > 0u && a.out_old > 0u);
    }

    /* B: 이중 차단 — 첫 차단 k1 (모든 op) × 복구 중 두 번째 차단 k2 (그 뒤 모든 op), TORN */
    {
        agg_t a;
        memset(&a, 0, sizeof a);
        uint32_t not_hit = 0;
        for (int sc = 0; sc < 2; sc++)
            for (uint32_t k1 = 1; k1 <= g_T[sc]; k1++) {
                life_t L;
                life_run(sc, &k1, 1, false, CUT_TORN, k1 * 7919u, &L);
                uint32_t end = L.ops;
                for (uint32_t k2 = k1 + 1; k2 <= end; k2++) {
                    uint32_t cuts[2] = { k1, k2 };
                    life_run(sc, cuts, 2, false, CUT_TORN, k1 * 7919u + k2, &L);
                    agg_add(&a, &L, k1 * 1000u + k2);
                    not_hit += L.cuts_hit != 2u;                     /* 두 차단 모두 실제로 일어났나 */
                }
            }
        agg_print("double cut (k1 x k2, torn)", &a);
        CHECK(a.scenarios > 10000u && not_hit == 0u);
        CHECK(a.viol == 0u);
    }

    /* B: 폭풍 — 1~5 번의 무작위 차단, 모드·시드 무작위 (고정 시드 3000 개) */
    {
        agg_t a;
        memset(&a, 0, sizeof a);
        uint32_t r = 0xC0FFEEu, hits = 0;
        for (uint32_t i = 0; i < 3000u; i++) {
            uint32_t cuts[5], n = 1u + xs32(&r) % 5u;
            for (uint32_t j = 0; j < n; j++) cuts[j] = 1u + xs32(&r) % 60u;
            int sc = (int)(xs32(&r) & 1u);
            uint32_t mode = xs32(&r) % 3u;
            life_t L;
            life_run(sc, cuts, n, true, mode, xs32(&r), &L);
            agg_add(&a, &L, i);
            hits += L.cuts_hit;
        }
        agg_print("storm (1-5 random cuts)", &a);
        printf("  power cuts actually hit: %u\n", hits);
        CHECK(a.scenarios == 3000u && a.viol == 0u && hits > 3000u);
    }
}

/* ---------- Part C ---------- */
static void delta_run(int sc, uint32_t er[MAX_SECTORS], uint64_t pb[MAX_SECTORS]) {
    life_t L;
    life_run(sc, NULL, 0, false, CUT_TORN, 1, &L);
    for (uint32_t s = 0; s < MAX_SECTORS; s++) { er[s] = g_f.erase_cnt[s]; pb[s] = g_f.prog_bytes[s]; }
}

static void test_part_c(void) {
    uint32_t er[2][MAX_SECTORS];
    uint64_t pb[2][MAX_SECTORS];
    delta_run(SC_CONFIRM, er[0], pb[0]);
    delta_run(SC_NOCONFIRM, er[1], pb[1]);
    /* 확인 경로: 메타 commit 14 = 요청 1 + 진행 3·4 (마지막이 TEST) + confirm 1 → 사본마다 7 */
    CHECK(er[0][SEC_META0] == 7u && er[0][SEC_META1] == 7u);
    CHECK(er[0][SEC_SCRATCH] == SLOT_SECTORS);
    int slots_ok = 1;
    for (uint32_t i = 0; i < SLOT_SECTORS; i++)
        slots_ok &= er[0][SEC_PRI + i] == 1u && er[0][SEC_SEC + i] == 2u &&
                    er[1][SEC_PRI + i] == 2u && er[1][SEC_SEC + i] == 3u;
    CHECK(slots_ok);
    /* 되돌림 경로: 요청 1 + 진행 12 + TEST→REVERT 1 + 진행 12 = 메타 26, scratch 8 */
    CHECK(er[1][SEC_META0] + er[1][SEC_META1] == 26u && er[1][SEC_SCRATCH] == 2u * SLOT_SECTORS);
    CHECK(er[0][SEC_BOOT] == 0u && pb[0][SEC_BOOT] == 0u);

    /* 손으로 검산 가능한 비용 예: 섹터마다 erase 10 ms, 8 B 당 20 µs */
    flash_timing_t t;
    memset(&t, 0, sizeof t);
    for (uint32_t s = 0; s < MAX_SECTORS; s++) t.erase_us[s] = 10000u;
    t.prog_unit_bytes = 8; t.prog_unit_us = 20; t.endurance = 10000;
    uint32_t e1[MAX_SECTORS] = { 0, 3, 1 };
    uint64_t p1[MAX_SECTORS] = { 0, 17, 64 };
    cost_t c;
    cost_estimate(e1, p1, 3, &t, &c);
    CHECK(c.erases == 4u && c.prog_units == 3u + 8u && c.time_us == 4u * 10000u + 11u * 20u &&
          c.hot_sector == 1u && c.hot_erases == 3u && c.updates_to_wearout == 3333u);
    uint32_t zero[MAX_SECTORS] = { 0 };
    uint64_t pz[MAX_SECTORS] = { 0 };
    cost_estimate(zero, pz, MAX_SECTORS, &t, &c);
    CHECK(c.time_us == 0u && c.updates_to_wearout == UINT32_MAX);

    /* 모델 그대로 */
    cost_estimate(er[0], pb[0], N_SECT, &t, &c);
    CHECK(c.hot_sector == SEC_META0 && c.hot_erases == 7u && c.updates_to_wearout == 10000u / 7u);
    printf("model update (confirm): erases=%u prog_units=%llu hot=sector %u x%u -> %u updates to wear-out\n",
           c.erases, (unsigned long long)c.prog_units, c.hot_sector, c.hot_erases, c.updates_to_wearout);

    /* F4 급으로 늘려 보기 (대표값, 데이터시트 확인 권장): 슬롯 섹터 128 KB erase 1 s, 메타 16 KB erase 0.25 s,
     * 32-bit word program 16 µs, 수명 10k. 데이터 섹터의 program 바이트는 ×(128K/256), 메타 레코드는 그대로 32 B */
    flash_timing_t f4;
    memset(&f4, 0, sizeof f4);
    for (uint32_t s = 0; s < MAX_SECTORS; s++) f4.erase_us[s] = 1000000u;
    f4.erase_us[SEC_META0] = f4.erase_us[SEC_META1] = 250000u;
    f4.prog_unit_bytes = 4; f4.prog_unit_us = 16; f4.endurance = 10000;
    const char *name[2] = { "confirm", "revert" };
    for (int sc = 0; sc < 2; sc++) {
        uint64_t scaled[MAX_SECTORS];
        for (uint32_t s = 0; s < MAX_SECTORS; s++)
            scaled[s] = (s == SEC_META0 || s == SEC_META1) ? pb[sc][s] : pb[sc][s] * (131072u / SECTOR_SIZE);
        cost_estimate(er[sc], scaled, N_SECT, &f4, &c);
        printf("F4-scale %-7s: erases=%u (meta %u, scratch %u) words=%llu time=%llu.%03llu s hot=sector %u -> %u updates\n",
               name[sc], c.erases, er[sc][SEC_META0] + er[sc][SEC_META1], er[sc][SEC_SCRATCH],
               (unsigned long long)c.prog_units, (unsigned long long)(c.time_us / 1000000u),
               (unsigned long long)(c.time_us % 1000000u / 1000u), c.hot_sector, c.updates_to_wearout);
        if (sc == 0) CHECK(c.time_us > 20000000u && c.time_us < 40000000u);   /* 수십 초 규모 */
    }
    if (g_verbose) {
        printf("erase count per sector (confirm | revert):\n");
        for (uint32_t s = 0; s < N_SECT; s++)
            printf("  sector %2u %-9s %u | %u\n", s,
                   s == SEC_BOOT ? "boot" : s <= SEC_META1 ? "meta" : s < SEC_SEC ? "primary" : s < SEC_SCRATCH ? "secondary" : "scratch",
                   er[0][s], er[1][s]);
    }
}

/* ---------- 부팅 로그 (-v) ---------- */
static void boot_log_demo(void) {
    printf("--- boot log: confirm scenario, torn erase at op 51, then torn program at op 60 ---\n");
    uint32_t cuts[2] = { 51, 60 };
    life_t L;
    g_log = true;
    life_run(SC_CONFIRM, cuts, 2, false, CUT_TORN, 42, &L);
    g_log = false;
    printf("--- result: outcome=%s boots=%u cuts=%u violations=%s ---\n",
           L.out == OUT_NEW ? "NEW(v1.3.0 confirmed)" : L.out == OUT_OLD_REVERTED ? "OLD(reverted)" : "none",
           L.boots, L.cuts_hit, V_NAME[L.viol]);
}

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    test_part_a();
    test_part_b();
    test_part_c();
    if (g_verbose) boot_log_demo();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
