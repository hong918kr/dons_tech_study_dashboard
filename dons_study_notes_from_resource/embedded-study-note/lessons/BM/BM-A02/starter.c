/* BM-A02 플래시 드라이버 시뮬레이터: 섹터 erase/program 규칙
 *
 * Part A — 플래시 모델 (BM-A02 가 소유하는 공용 어휘 — A05/A06 은 축약판을 embed):
 *          erase = 섹터 전체 0xFF, program = AND (1→0 만), 쓰기 단위(prog_unit 1/4/8) 정렬,
 *          F4 식 불균일 섹터 (16/16/16/16/64/128×7 KB).
 *          드라이버: drv_write (쓰기 전 (old & new) == new 사전검사, 비정렬 head/tail 은 read-merge,
 *          섹터 경계 넘기, 단위마다 read-back verify), drv_erase_range (시작 = 섹터 경계 필수,
 *          끝 = 그 섹터 끝까지 확장, erase 후 blank verify).
 * Part B — 고장 주입: erase 없이 재프로그램, 일부만 충돌하는 쓰기(전부 아니면 전무), ECC 식 1회 프로그램
 *          (one_shot), 닳은 셀(stuck bit) → VERIFY, 각본 시퀀스의 모든 op 에서 전원 차단(torn erase/program)
 *          → 부팅 때 CRC·blank check 로 잡히는 것 / 안 잡히는 것(weak erase), 복구 정책 두 가지 비교.
 * Part C — wear: 섹터별 erase 카운터, 로그 구조(append) vs 제자리 재작성(naive) erase 히스토그램,
 *          부팅 스캔(log_mount) + 전원 차단 캠페인.
 *
 * 레지스터 없는 "동작 모델" 이다. STM32F4 (RM0090) 섹터 표·PSIZE 는 대표값 — RM 확인 권장.
 * 테스트의 정답은 이 모델의 정의다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   (TODO 8 곳을 채우기 전엔 FAIL, exit 1)
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
    /* TODO A2: 전원이 이미 나갔으면 POWER_CUT. 섹터 번호 검사(RANGE). op 를 센다(flash_tick) —
     *          wear 카운터(n_erase, erase_count[s])도 올린다. 찢어지면 tear_erase 후 POWER_CUT.
     *          정상이면 섹터 전체 0xFF, weak 비트 0, 0 고착 셀(stuck_level 0)은 0 으로 남는다. */
    (void)f; (void)sector;
    return FLASH_OK;
}

flash_rc_t flash_program(flash_t *f, uint32_t addr, const uint8_t *src, uint32_t len) {
    /* TODO A1: 전원 검사 → 범위(RANGE) → 정렬(addr, len 모두 prog_unit 배수, 아니면 ALIGN) →
     *          one_shot 이면 대상 바이트가 전부 0xFF 여야(NOT_ERASED) → flash_tick + n_prog++ →
     *          단위마다 mem &= (src | 1 고착 비트).  찢어지는 op 는 rng 로 고른 단위 하나를
     *          tear_program 하고 POWER_CUT (그 앞 단위는 완료, 뒤 단위는 안 됨). */
    (void)f; (void)addr; (void)src; (void)len;
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
    /* TODO A3: len 0 → OK, 범위 → RANGE.  쓰기 범위를 감싸는 단위 [u_first, u_end) 를 구한다.
     *          (1) 사전검사: 내 바이트마다 (old & new) == new, one_shot 이면 단위 전체가 0xFF —
     *              하나라도 어기면 아무것도 쓰지 않고 NOT_ERASED.
     *          (2) 단위마다: 현재 내용을 읽어 내 바이트만 덮어쓰고(read-merge) flash_program,
     *              rc != OK 면 그대로 반환, 다시 읽어 비교 → 다르면 VERIFY. */
    (void)f; (void)addr; (void)buf; (void)len;
    return FLASH_OK;
}

flash_rc_t drv_erase_range(flash_t *f, uint32_t addr, uint32_t len) {
    /* TODO A4: len 0 → OK, 범위 → RANGE.  addr 는 섹터 시작이어야(아니면 ALIGN — 앞쪽 확장 금지),
     *          끝은 addr+len-1 이 든 섹터의 끝까지 확장.  섹터마다 erase → blank check → 아니면 VERIFY. */
    (void)f; (void)addr; (void)len;
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
    /* TODO B1: payload n B + CRC 4 B 를 flash_read 로 읽고 crc32_calc(payload) == 저장된 CRC(LE) 인지. */
    (void)f; (void)addr; (void)n;
    return false;
}

/* 각본: 4 × 256 B, prog_unit 4.  섹터 1·2 를 준비하고 레코드 A(섹터 경계를 넘음), B 를 쓴다. */
#define B_BASE   0x08000000u
#define B_SEC    256u
#define REC_A    (B_BASE + 1u * B_SEC + 250u)        /* 0x080001FA: 20 B → 0x0800020E, 섹터 1→2 */
#define REC_A_N  16u
#define REC_B    (B_BASE + 2u * B_SEC + 44u)         /* 0x0800022C: 37 B */
#define REC_B_N  33u
static uint8_t g_pay_a[REC_A_N], g_pay_b[REC_B_N];

typedef enum { POL_ALWAYS_ERASE, POL_SKIP_IF_BLANK } erase_policy_t;

flash_rc_t prepare_sector(flash_t *f, uint32_t s, erase_policy_t pol) {
    uint32_t a = f->base + flash_sector_start(f, s), n = flash_sector_len(f, s);
    if (pol == POL_SKIP_IF_BLANK && drv_blank_check(f, a, n)) return FLASH_OK;   /* "이미 비었네" 최적화 */
    return drv_erase_range(f, a, n);
}
flash_rc_t store_records(flash_t *f, erase_policy_t pol) {
    flash_rc_t rc;
    if ((rc = prepare_sector(f, 1, pol)) != FLASH_OK) return rc;
    if ((rc = prepare_sector(f, 2, pol)) != FLASH_OK) return rc;
    if ((rc = rec_write(f, REC_A, g_pay_a, REC_A_N)) != FLASH_OK) return rc;
    return rec_write(f, REC_B, g_pay_b, REC_B_N);
}
/* 부팅 때 호출: 0 = 둘 다 유효(손대지 않음), 1 = 다시 썼음, -1 = 다시 쓰기 실패 */
int boot_recover(flash_t *f, erase_policy_t pol) {
    /* TODO B2: A, B 가 둘 다 rec_valid 면 0.  아니면 store_records(f, pol) 로 처음부터 다시 쓰고
     *          성공이면 1, 실패면 -1.  (정책 pol 은 그대로 넘긴다 — 정책의 차이는 테스트가 보여 준다) */
    (void)f; (void)pol;
    return 0;
}

/* ======================= Part C: 로그 구조 저장 (wear) ======================= */
#define LOG_REC 32u                                   /* seq(LE32) | data[24] | crc32(seq+data, LE) */
#define LOG_PAY 24u
typedef struct { flash_t *f; uint32_t s_first, n_sec, cur, slot, seq; } log_t;

void log_pack(uint8_t out[LOG_REC], uint32_t seq, const uint8_t *data) {
    for (int i = 0; i < 4; i++) out[i] = (uint8_t)(seq >> (8 * i));
    memcpy(out + 4, data, LOG_PAY);
    uint32_t c = crc32_calc(out, 4 + LOG_PAY);
    for (int i = 0; i < 4; i++) out[28 + i] = (uint8_t)(c >> (8 * i));
}
bool log_unpack(const uint8_t in[LOG_REC], uint32_t *seq, uint8_t *data) {
    uint32_t c = (uint32_t)in[28] | (uint32_t)in[29] << 8 | (uint32_t)in[30] << 16 | (uint32_t)in[31] << 24;
    if (crc32_calc(in, 4 + LOG_PAY) != c) return false;
    *seq = (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
    if (data) memcpy(data, in + 4, LOG_PAY);
    return true;
}
uint32_t log_slot_addr(const log_t *lg, uint32_t s, uint32_t slot) {
    return lg->f->base + flash_sector_start(lg->f, s) + slot * LOG_REC;
}
flash_rc_t log_format(log_t *lg, flash_t *f, uint32_t s_first, uint32_t n_sec) {
    lg->f = f; lg->s_first = s_first; lg->n_sec = n_sec; lg->cur = s_first; lg->slot = 0; lg->seq = 0;
    for (uint32_t s = s_first; s < s_first + n_sec; s++) {
        flash_rc_t rc = drv_erase_range(f, f->base + flash_sector_start(f, s), flash_sector_len(f, s));
        if (rc != FLASH_OK) return rc;
    }
    return FLASH_OK;
}
/* 부팅 스캔: 유효 레코드 중 seq 최대를 찾는다.  찾으면 true + *seq_out/data_out, 쓰기 위치 설정 */
bool log_mount(log_t *lg, flash_t *f, uint32_t s_first, uint32_t n_sec, uint32_t *seq_out, uint8_t *data_out) {
    lg->f = f; lg->s_first = s_first; lg->n_sec = n_sec; lg->cur = s_first; lg->slot = 0; lg->seq = 0;
    /* TODO C1: 모든 섹터의 모든 슬롯을 읽어 log_unpack 이 통과하는 레코드 중 seq 최대를 찾는다.
     *          없으면 false.  있으면 cur = 그 섹터, seq = 그 seq, slot = 그 섹터에서
     *          "마지막으로 0xFF 가 아닌 슬롯" 다음 (찢어진 슬롯은 건너뛴다 — 다시 쓰면 NOT_ERASED). */
    (void)seq_out; (void)data_out;
    return false;
}
flash_rc_t log_append(log_t *lg, const uint8_t *data) {
    /* TODO C2: 현재 섹터가 찼으면(slot >= 섹터 크기 / LOG_REC) 다음 섹터(원형)를 drv_erase_range 로
     *          지우고 cur/slot 이동.  seq+1 로 log_pack → drv_write.  성공/실패와 무관하게 slot++
     *          (찢어졌을 수 있는 슬롯은 다시 안 쓴다), 성공일 때만 seq++. */
    (void)lg; (void)data;
    return FLASH_OK;
}
/* 비교 대상: 매번 섹터 0 을 지우고 같은 자리에 다시 쓰기 */
flash_rc_t naive_update(flash_t *f, uint32_t seq, const uint8_t *data) {
    uint8_t b[LOG_REC];
    flash_rc_t rc = drv_erase_range(f, f->base, flash_sector_len(f, 0));
    if (rc != FLASH_OK) return rc;
    log_pack(b, seq, data);
    return drv_write(f, f->base, b, LOG_REC);
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total, g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static flash_t g_f;                                   /* 2 MB 넘는 구조체 — 정적 하나를 재사용 */
static const uint32_t F4_SIZES[12] = { 16384, 16384, 16384, 16384, 65536,
    131072, 131072, 131072, 131072, 131072, 131072, 131072 };
static uint32_t g_rng = 0xC0FFEE11u;
static uint32_t rnd(void) { return xorshift32(&g_rng); }
static void fill_raw(flash_t *f, uint32_t off, uint32_t len, uint32_t seed) {   /* 테스트 뒷문: 옛 데이터 */
    for (uint32_t i = 0; i < len; i++) f->mem[off + i] = (uint8_t)xorshift32(&seed);
}

static void test_part_a(void) {
    flash_t *f = &g_f;
    /* A-1 F4 지오메트리 */
    CHECK(flash_init(f, 0x08000000u, F4_SIZES, 0, 12, 4));
    CHECK(flash_total(f) == 1024u * 1024u);
    CHECK(flash_sector_start(f, 4) == 0x10000u && flash_sector_start(f, 5) == 0x20000u &&
          flash_sector_start(f, 11) == 0xE0000u);
    {
        static const uint32_t starts[13] = { 0, 0x4000, 0x8000, 0xC000, 0x10000, 0x20000, 0x40000, 0x60000,
                                             0x80000, 0xA0000, 0xC0000, 0xE0000, 0x100000 };
        uint32_t bad = 0, s = 0;
        for (uint32_t off = 0; off < 0x100000u; off++) {
            while (off >= starts[s + 1]) s++;
            if (flash_sector_of(f, 0x08000000u + off) != (int)s) bad++;
        }
        CHECK(bad == 0);
    }
    CHECK(flash_sector_of(f, 0x07FFFFFFu) == -1 && flash_sector_of(f, 0x08100000u) == -1);

    /* A-2 AND 의미 — 바이트 쌍 65 536 개 전수 (raw 모델, prog_unit 1) */
    CHECK(flash_init(f, 0x08000000u, NULL, 256, 4, 1));
    {
        uint32_t bad = 0;
        for (uint32_t a = 0; a < 256; a++) {
            uint8_t va = (uint8_t)a, vb, got[256];
            if (flash_erase_sector(f, 0) != FLASH_OK) bad++;
            for (uint32_t b = 0; b < 256; b++) {
                vb = (uint8_t)b;
                if (flash_program(f, 0x08000000u + b, &va, 1) != FLASH_OK) bad++;
                if (flash_program(f, 0x08000000u + b, &vb, 1) != FLASH_OK) bad++;   /* erase 없이 두 번째 */
            }
            flash_read(f, 0x08000000u, got, 256);
            for (uint32_t b = 0; b < 256; b++) if (got[b] != (uint8_t)(a & b)) bad++;
        }
        CHECK(bad == 0);
        CHECK(f->n_erase == 256 && f->n_prog == 2u * 65536u);
    }
    /* A-3 raw 모델의 정렬·범위 검사 (prog_unit 4) */
    CHECK(flash_init(f, 0x08000000u, NULL, 256, 4, 4));
    {
        uint8_t d[8] = { 0 };
        CHECK(flash_program(f, 0x08000002u, d, 4) == FLASH_ERR_ALIGN);
        CHECK(flash_program(f, 0x08000004u, d, 6) == FLASH_ERR_ALIGN);
        CHECK(flash_program(f, 0x080003FCu, d, 8) == FLASH_ERR_RANGE);
        CHECK(flash_program(f, 0x07FFFFFCu, d, 4) == FLASH_ERR_RANGE);
        CHECK(flash_erase_sector(f, 4) == FLASH_ERR_RANGE);
        CHECK(f->n_prog == 0 && f->n_erase == 0 && f->op_index == 0);   /* 거절된 호출은 op 가 아니다 */
        CHECK(drv_blank_check(f, 0x08000000u, 1024));
    }
    /* A-4 드라이버의 사전검사 — 바이트 쌍 전수: NOT_ERASED ⇔ (a & b) != b, 거절이면 그대로 */
    CHECK(flash_init(f, 0x08000000u, NULL, 256, 4, 1));
    {
        uint32_t bad = 0, refused = 0;
        for (uint32_t a = 0; a < 256; a++) {
            uint8_t va = (uint8_t)a, got[256];
            (void)flash_erase_sector(f, 0);
            for (uint32_t b = 0; b < 256; b++) (void)flash_program(f, 0x08000000u + b, &va, 1);
            for (uint32_t b = 0; b < 256; b++) {
                uint8_t vb = (uint8_t)b;
                flash_rc_t rc = drv_write(f, 0x08000000u + b, &vb, 1);
                bool should = (a & b) == b;
                if ((rc == FLASH_OK) != should) bad++;
                if (!should && rc != FLASH_ERR_NOT_ERASED) bad++;
                refused += rc == FLASH_ERR_NOT_ERASED;
            }
            flash_read(f, 0x08000000u, got, 256);
            for (uint32_t b = 0; b < 256; b++)
                if (got[b] != (((a & b) == b) ? (uint8_t)b : (uint8_t)a)) bad++;
        }
        CHECK(bad == 0);
        CHECK(refused == 65536u - 6561u);          /* (a&b)==b 인 쌍 = 3^8 = 6561 */
    }
    /* A-5 비정렬 head/tail 전수: unit 1/4/8 × off 0..15 × len 0..40 */
    {
        static const uint32_t units[3] = { 1, 4, 8 };
        uint32_t bad = 0, cases = 0;
        for (int ui = 0; ui < 3; ui++)
            for (uint32_t off = 0; off < 16; off++)
                for (uint32_t len = 0; len <= 40; len++) {
                    const uint32_t u = units[ui];
                    flash_init(f, 0x08000000u, NULL, 256, 2, u);
                    uint8_t d[40], got[96];
                    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)(0x11 * (i + 1) + off);
                    uint32_t p0 = f->n_prog;
                    if (drv_write(f, 0x08000000u + 8 + off, d, len) != FLASH_OK) bad++;
                    uint32_t lo = 8 + off, hi = lo + len;
                    uint32_t expect_units = len ? ((hi + u - 1) / u - lo / u) : 0;
                    if (f->n_prog - p0 != expect_units) bad++;
                    flash_read(f, 0x08000000u, got, 96);
                    for (uint32_t i = 0; i < 96; i++) {
                        uint8_t e = (i >= lo && i < hi) ? d[i - lo] : 0xFF;
                        if (got[i] != e) bad++;
                    }
                    cases++;
                }
        CHECK(bad == 0 && cases == 3u * 16u * 41u);
    }
    /* A-6 같은 단위를 나눠 쓰는 두 레코드 (AND 플래시: read-merge 로 이웃 보존) */
    CHECK(flash_init(f, 0x08000000u, NULL, 256, 2, 8));
    {
        const uint8_t r1[5] = { 1, 2, 3, 4, 5 }, r2[6] = { 6, 7, 8, 9, 10, 11 };
        uint8_t got[16];
        CHECK(drv_write(f, 0x08000000u, r1, 5) == FLASH_OK);
        CHECK(drv_write(f, 0x08000005u, r2, 6) == FLASH_OK);
        flash_read(f, 0x08000000u, got, 16);
        CHECK(memcmp(got, r1, 5) == 0 && memcmp(got + 5, r2, 6) == 0 && got[11] == 0xFF && got[15] == 0xFF);
        CHECK(f->n_prog == 3);                    /* 단위 0, 단위 0 (두 번째), 단위 1 */
    }
    /* A-7 섹터 경계 넘기: F4 16K→16K, 16K→64K, 64K→128K */
    CHECK(flash_init(f, 0x08000000u, F4_SIZES, 0, 12, 4));
    {
        static const uint32_t at[3] = { 0x08003FFAu, 0x0800FFFAu, 0x0801FFF9u };
        uint32_t bad = 0;
        for (int i = 0; i < 3; i++) {
            uint8_t d[13], got[13];
            for (int k = 0; k < 13; k++) d[k] = (uint8_t)(0xA0 + i * 16 + k);
            if (drv_write(f, at[i], d, 13) != FLASH_OK) bad++;
            flash_read(f, at[i], got, 13);
            if (memcmp(got, d, 13) != 0) bad++;
            if (flash_sector_of(f, at[i]) + 1 != flash_sector_of(f, at[i] + 12)) bad++;
        }
        CHECK(bad == 0);
    }
    /* A-8 erase_range: 시작 정렬 필수, 끝은 섹터 끝까지 확장 */
    {
        uint8_t mark = 0x5A, got;
        CHECK(drv_write(f, 0x0800BFFCu, &mark, 1) == FLASH_OK);     /* 섹터 2 마지막 근처 */
        CHECK(drv_write(f, 0x0800C010u, &mark, 1) == FLASH_OK);     /* 섹터 3 */
        CHECK(drv_write(f, 0x0801FFF0u, &mark, 1) == FLASH_OK);     /* 섹터 4 끝 근처 */
        CHECK(drv_write(f, 0x08020010u, &mark, 1) == FLASH_OK);     /* 섹터 5 (A-7 이 쓴 바이트 옆) */
        uint32_t e0 = f->n_erase;
        CHECK(drv_erase_range(f, 0x0800C000u, 0x5000u) == FLASH_OK);  /* 3 (16K) + 4 (64K) → 80 KB */
        CHECK(f->n_erase - e0 == 2 && f->erase_count[3] == 1 && f->erase_count[4] == 1);
        flash_read(f, 0x0801FFF0u, &got, 1);  CHECK(got == 0xFF);   /* 요청(0x5000) 밖이지만 섹터 4 → 지워짐 */
        flash_read(f, 0x0800BFFCu, &got, 1);  CHECK(got == 0x5A);   /* 섹터 2 는 그대로 */
        flash_read(f, 0x08020010u, &got, 1);  CHECK(got == 0x5A);   /* 섹터 5 는 그대로 */
        e0 = f->n_erase;
        CHECK(drv_erase_range(f, 0x0800C004u, 4) == FLASH_ERR_ALIGN);
        CHECK(drv_erase_range(f, 0x080E0000u, 0x20001u) == FLASH_ERR_RANGE);
        CHECK(drv_erase_range(f, 0x08000000u, 0xFFFFFFFFu) == FLASH_ERR_RANGE);
        CHECK(drv_erase_range(f, 0x07FFC000u, 0x4000u) == FLASH_ERR_RANGE);
        CHECK(drv_erase_range(f, 0x08000000u, 0) == FLASH_OK);
        CHECK(drv_write(f, 0x080FFFFFu, &mark, 2) == FLASH_ERR_RANGE);
        CHECK(f->n_erase == e0);
    }
    /* A-9 무작위 4000 연산 vs 기준 모델 (8 × 512 B, unit 4) */
    CHECK(flash_init(f, 0x08000000u, NULL, 512, 8, 4));
    {
        static uint8_t ref[4096];
        memset(ref, 0xFF, sizeof ref);
        uint32_t bad = 0, n_ok = 0, n_ne = 0;
        for (int it = 0; it < 4000; it++) {
            uint32_t r = rnd() % 100;
            if (r < 12) {
                uint32_t s = rnd() % 8;
                if (drv_erase_range(f, 0x08000000u + s * 512, 1 + rnd() % 512) != FLASH_OK) bad++;
                memset(ref + s * 512, 0xFF, 512);
            } else {
                uint32_t off = rnd() % 4096, len = 1 + rnd() % 64;
                if (off + len > 4096) len = 4096 - off;
                uint8_t d[64];
                for (uint32_t i = 0; i < len; i++) d[i] = (rnd() % 3 == 0) ? 0xFF : (uint8_t)rnd();
                bool legal = true;
                for (uint32_t i = 0; i < len; i++) if ((ref[off + i] & d[i]) != d[i]) legal = false;
                flash_rc_t rc = drv_write(f, 0x08000000u + off, d, len);
                if (legal) { if (rc != FLASH_OK) bad++; memcpy(ref + off, d, len); n_ok++; }
                else { if (rc != FLASH_ERR_NOT_ERASED) bad++; n_ne++; }
            }
        }
        CHECK(memcmp(f->mem, ref, 4096) == 0);
        CHECK(bad == 0 && n_ok > 300 && n_ne > 300);
        if (g_verbose) printf("  A-9: 4000 ops, writes ok %u, refused %u, erases %u\n", n_ok, n_ne, f->n_erase);
    }
}

/* ---- Part B ---- */
typedef struct {
    uint32_t n_ops, scen, cut_ret, false_accept, phase[5], blank_pass_01, blank_pass_2, silent_weak,
             erase_raised_only, prog_subset_ok, needed_recover, recover_fail, ok_after_age, bad_after_age;
} campaign_t;

static void init_b(flash_t *f, uint32_t seed) {
    flash_init(f, B_BASE, NULL, B_SEC, 4, 4);
    fill_raw(f, B_SEC, 2 * B_SEC, seed * 2654435761u + 1u);         /* 섹터 1·2 에 옛 데이터 */
    f->rng = seed * 0x9E3779B9u + 0x1234567u;
}
static bool bytes_match(const flash_t *f, uint32_t addr, const uint8_t *pay, uint32_t n) {
    uint8_t b[REC_MAX_PAYLOAD + 4];
    flash_read(f, addr, b, n);
    return memcmp(b, pay, n) == 0;
}
static void campaign(erase_policy_t pol, uint32_t seeds, campaign_t *c) {
    flash_t *f = &g_f;
    memset(c, 0, sizeof *c);
    init_b(f, 0);
    if (store_records(f, pol) != FLASH_OK) return;
    c->n_ops = f->op_index;                                          /* 전원 차단 없는 1회 = N op */
    uint8_t before[2 * B_SEC];
    for (uint32_t k = 1; k <= c->n_ops; k++)
        for (uint32_t sd = 1; sd <= seeds; sd++) {
            init_b(f, sd);
            memcpy(before, f->mem + B_SEC, sizeof before);
            f->cut_at = k;
            flash_rc_t rc = store_records(f, pol);
            c->scen++;
            c->cut_ret += rc == FLASH_POWER_CUT;
            c->phase[f->last_tear]++;
            flash_power_on(f);                                       /* --- 재부팅 --- */
            const bool a = rec_valid(f, REC_A, REC_A_N), b = rec_valid(f, REC_B, REC_B_N);
            c->false_accept += (a && !bytes_match(f, REC_A, g_pay_a, REC_A_N)) ||
                               (b && !bytes_match(f, REC_B, g_pay_b, REC_B_N));
            if (f->last_tear < 4) {                                  /* 찢어진 건 erase: k 번째 = 섹터 k */
                const uint32_t so = k * B_SEC;
                bool raised = true, weak = false;
                for (uint32_t i = 0; i < B_SEC; i++) {
                    if ((f->mem[so + i] & before[so - B_SEC + i]) != before[so - B_SEC + i]) raised = false;
                    if (f->weak[so + i]) weak = true;
                }
                c->erase_raised_only += raised;
                const bool blank = drv_blank_check(f, B_BASE + so, B_SEC);
                if (f->last_tear <= 1) c->blank_pass_01 += blank;
                if (f->last_tear == 2) c->blank_pass_2 += blank;
                c->silent_weak += blank && weak;
            } else {
                c->prog_subset_ok++;                                 /* torn program 은 아래 별도 검사 */
            }
            c->needed_recover += !(a && b);
            int r = boot_recover(f, pol);
            c->recover_fail += r < 0;
            flash_age(f);                                            /* --- 시간이 흐른다 --- */
            const bool ok = rec_valid(f, REC_A, REC_A_N) && rec_valid(f, REC_B, REC_B_N);
            c->ok_after_age += ok;
            c->bad_after_age += !ok;
        }
}

static void test_part_b(void) {
    flash_t *f = &g_f;
    uint8_t v, got[64];
    /* B-1 erase 없이 재프로그램: raw 는 조용히 AND */
    CHECK(flash_init(f, 0x08000000u, NULL, 256, 4, 1));
    v = 0x55; CHECK(flash_program(f, 0x08000010u, &v, 1) == FLASH_OK);
    v = 0xAA; CHECK(flash_program(f, 0x08000010u, &v, 1) == FLASH_OK);
    flash_read(f, 0x08000010u, got, 1); CHECK(got[0] == 0x00);
    /* B-2 드라이버: 0x55 위 0xAA 거절, 0x55 위 0x54 (상태 플래그 내리기) 허용 */
    v = 0x55; CHECK(drv_write(f, 0x08000020u, &v, 1) == FLASH_OK);
    v = 0xAA; CHECK(drv_write(f, 0x08000020u, &v, 1) == FLASH_ERR_NOT_ERASED);
    flash_read(f, 0x08000020u, got, 1); CHECK(got[0] == 0x55);
    v = 0x54; CHECK(drv_write(f, 0x08000020u, &v, 1) == FLASH_OK);
    flash_read(f, 0x08000020u, got, 1); CHECK(got[0] == 0x54);
    /* B-3 전부 아니면 전무: 64 B 중 마지막 1 B 만 충돌 → 한 바이트도 안 씀 */
    {
        uint8_t d[64];
        memset(d, 0x00, sizeof d);
        v = 0x0F; CHECK(drv_write(f, 0x0800013Fu, &v, 1) == FLASH_OK);
        for (int i = 0; i < 64; i++) d[i] = (uint8_t)i;
        d[63] = 0xF0;                                   /* 0x0F 위에 0xF0 → 0→1 필요 */
        uint32_t p0 = f->n_prog;
        CHECK(drv_write(f, 0x08000100u, d, 64) == FLASH_ERR_NOT_ERASED);
        CHECK(f->n_prog == p0 && drv_blank_check(f, 0x08000100u, 63));
    }
    /* B-4 ECC 식 1회 프로그램 (one_shot, unit 8): 이웃과 단위를 나누면 두 번째가 거절된다 */
    CHECK(flash_init(f, 0x08000000u, NULL, 256, 2, 8));
    f->one_shot = true;
    {
        const uint8_t r1[5] = { 1, 2, 3, 4, 5 }, r2[6] = { 6, 7, 8, 9, 10, 11 };
        CHECK(drv_write(f, 0x08000000u, r1, 5) == FLASH_OK);
        uint32_t p0 = f->n_prog;
        CHECK(drv_write(f, 0x08000005u, r2, 6) == FLASH_ERR_NOT_ERASED);
        CHECK(f->n_prog == p0);
        CHECK(drv_write(f, 0x08000008u, r2, 6) == FLASH_OK);     /* 다음 단위 경계부터는 OK */
        uint8_t z[8] = { 0 };
        CHECK(flash_program(f, 0x08000000u, z, 8) == FLASH_ERR_NOT_ERASED);   /* 모델도 거절 */
        flash_read(f, 0x08000000u, got, 16);
        CHECK(memcmp(got, r1, 5) == 0 && got[5] == 0xFF && memcmp(got + 8, r2, 6) == 0);
    }
    /* B-5 닳은 셀: 1 고착 → program verify 실패, 0 고착 → erase verify 실패 */
    CHECK(flash_init(f, 0x08000000u, NULL, 256, 2, 4));
    f->stuck_off = 0x41; f->stuck_mask = 0x10; f->stuck_level = 1;
    {
        uint8_t d[4] = { 0x00, 0x00, 0x00, 0x00 };
        CHECK(drv_write(f, 0x08000040u, d, 4) == FLASH_ERR_VERIFY);
        flash_read(f, 0x08000041u, got, 1); CHECK(got[0] == 0x10);
        uint8_t e[4] = { 0x00, 0x10, 0x00, 0x00 };
        CHECK(drv_write(f, 0x08000044u, e, 4) == FLASH_OK);       /* 다른 단위 */
        f->stuck_level = 0;
        CHECK(drv_erase_range(f, 0x08000000u, 256) == FLASH_ERR_VERIFY);
        CHECK(drv_erase_range(f, 0x08000100u, 256) == FLASH_OK);
    }
    /* B-6 전원 차단 뒤엔 모든 op 가 POWER_CUT, 재부팅 뒤 정상 */
    CHECK(flash_init(f, 0x08000000u, NULL, 256, 2, 4));
    {
        uint8_t d[4] = { 0x12, 0x34, 0x56, 0x78 };
        f->cut_at = 2;
        CHECK(drv_write(f, 0x08000000u, d, 4) == FLASH_OK);          /* op 1 */
        CHECK(flash_erase_sector(f, 1) == FLASH_POWER_CUT);          /* op 2: 찢어짐 (섹터 1 은 비어 있었음) */
        CHECK(drv_write(f, 0x08000004u, d, 4) == FLASH_POWER_CUT);
        CHECK(flash_erase_sector(f, 0) == FLASH_POWER_CUT);
        flash_read(f, 0x08000000u, got, 8);
        CHECK(memcmp(got, d, 4) == 0 && got[4] == 0xFF);             /* 죽은 뒤엔 아무 일도 없었다 */
        CHECK(f->op_index == 2);
        flash_power_on(f);
        CHECK(drv_write(f, 0x08000004u, d, 4) == FLASH_OK);
    }
    /* B-7 torn program 의 성질 (seed 2000 개): 앞 단위 완료, 뒤 단위 미기록, 찢어진 단위는 0→1 없음 */
    {
        uint32_t bad = 0, partial = 0;
        for (uint32_t sd = 1; sd <= 2000; sd++) {
            flash_init(f, 0x08000000u, NULL, 256, 1, 4);
            f->rng = sd;
            uint8_t d[32];
            for (int i = 0; i < 32; i++) d[i] = (uint8_t)(i * 37 + sd);
            f->cut_at = 1;
            if (flash_program(f, 0x08000000u, d, 32) != FLASH_POWER_CUT) bad++;
            uint32_t mid = 0, first_not_full = 8, blank_after = 0;
            for (uint32_t un = 0; un < 8; un++) {
                bool is_full = memcmp(f->mem + un * 4, d + un * 4, 4) == 0, is_blank = true, sub_ok = true;
                for (int i = 0; i < 4; i++) {
                    uint8_t m = f->mem[un * 4 + i], w = d[un * 4 + i];
                    if (m != 0xFF) is_blank = false;
                    if ((m & w) != w) sub_ok = false;                /* 1 이어야 할 비트가 0 이 되진 않았다 */
                }
                if (!sub_ok) bad++;
                if (!is_full && first_not_full == 8) first_not_full = un;
                if (!is_full && !is_blank) mid++;
                if (un > first_not_full && is_blank) blank_after++;
            }
            if (mid > 1) bad++;                                      /* 중간 상태는 최대 한 단위 */
            if (first_not_full < 8 && blank_after != 7 - first_not_full) bad++;   /* 그 뒤는 전부 미기록 */
            partial += mid;
        }
        CHECK(bad == 0 && partial > 1500);
    }
    /* B-8 각본 시퀀스의 모든 op × seed 64 에서 전원 차단 */
    for (uint32_t i = 0; i < REC_A_N; i++) g_pay_a[i] = (uint8_t)(0xA0 + i);
    for (uint32_t i = 0; i < REC_B_N; i++) g_pay_b[i] = (uint8_t)(0x30 + 7 * i);
    campaign_t good, bad;
    campaign(POL_ALWAYS_ERASE, 64, &good);
    campaign(POL_SKIP_IF_BLANK, 64, &bad);
    CHECK(good.n_ops == 18 && bad.n_ops == 18);                      /* erase 2 + A 6 단위 + B 10 단위 */
    CHECK(good.scen == 18u * 64u && good.cut_ret == good.scen);
    CHECK(good.false_accept == 0 && bad.false_accept == 0);          /* 찢어진 레코드를 받아들인 적 없음 */
    CHECK(good.phase[0] && good.phase[1] && good.phase[2] && good.phase[3] && good.phase[4]);
    CHECK(good.erase_raised_only == good.phase[0] + good.phase[1] + good.phase[2] + good.phase[3]);
    CHECK(good.blank_pass_01 == 0);                                  /* 초·중반 차단은 blank check 가 잡음 */
    CHECK(good.blank_pass_2 == good.phase[2] && good.silent_weak > 0);   /* 후반 차단은 못 잡음 */
    CHECK(good.recover_fail == 0 && good.ok_after_age == good.scen); /* 항상 erase → 시간이 지나도 OK */
    CHECK(bad.recover_fail == 0 && bad.bad_after_age > 0 && bad.bad_after_age <= bad.silent_weak);
    printf("Part B: N=%u ops x 64 seeds = %u scenarios | phase0 %u phase1 %u phase2 %u phase3 %u program %u\n",
           good.n_ops, good.scen, good.phase[0], good.phase[1], good.phase[2], good.phase[3], good.phase[4]);
    printf("Part B: false-accept %u | silent weak (blank check passed) %u | needed recovery %u\n",
           good.false_accept, good.silent_weak, good.needed_recover);
    printf("Part B: after age  ALWAYS_ERASE ok %u/%u | SKIP_IF_BLANK corrupt %u/%u\n",
           good.ok_after_age, good.scen, bad.bad_after_age, bad.scen);
}

/* ---- Part C ---- */
static void pay_of(uint32_t seq, uint8_t *d) { for (uint32_t i = 0; i < LOG_PAY; i++) d[i] = (uint8_t)(seq * 31 + i); }

static void hist(const char *name, const flash_t *f, uint32_t n) {
    uint32_t mx = 1;
    for (uint32_t s = 0; s < n; s++) if (f->erase_count[s] > mx) mx = f->erase_count[s];
    printf("  %s\n", name);
    for (uint32_t s = 0; s < n; s++) {
        int bar = (int)((uint64_t)f->erase_count[s] * 40u / mx);
        printf("    sector %u %6u |%.*s\n", s, f->erase_count[s], bar, "########################################");
    }
}

static void test_part_c(void) {
    flash_t *f = &g_f;
    const uint32_t U = 10000;
    uint8_t d[LOG_PAY], out[LOG_PAY];
    CHECK(crc32_calc((const uint8_t *)"123456789", 9) == 0xCBF43926u);
    /* C-1 naive: 매번 섹터 0 erase + 같은 자리 쓰기 */
    CHECK(flash_init(f, 0x08000000u, NULL, 1024, 4, 4));
    uint32_t bad = 0;
    for (uint32_t i = 1; i <= U; i++) { pay_of(i, d); if (naive_update(f, i, d) != FLASH_OK) bad++; }
    CHECK(bad == 0);
    CHECK(f->erase_count[0] == U && f->erase_count[1] == 0 && f->n_erase == U);
    uint32_t naive_max = f->erase_count[0];
    if (g_verbose) hist("naive rewrite (10000 updates)", f, 4);
    /* C-2 로그 구조: 4 섹터 원형, 32 레코드/섹터 */
    CHECK(flash_init(f, 0x08000000u, NULL, 1024, 4, 4));
    log_t lg;
    CHECK(log_format(&lg, f, 0, 4) == FLASH_OK);
    bad = 0;
    for (uint32_t i = 1; i <= U; i++) { pay_of(i, d); if (log_append(&lg, d) != FLASH_OK) bad++; }
    CHECK(bad == 0 && lg.seq == U);
    uint32_t mx = 0, mn = UINT32_MAX;
    for (uint32_t s = 0; s < 4; s++) { if (f->erase_count[s] > mx) mx = f->erase_count[s]; if (f->erase_count[s] < mn) mn = f->erase_count[s]; }
    CHECK(f->n_erase == 4u + ((U + 31u) / 32u - 1u));                /* format 4 + 회전 312 = 316 */
    CHECK(mx - mn <= 1 && naive_max / mx >= 100);
    printf("Part C: %u updates | naive max erase/sector %u | log total erases %u, per sector %u..%u | ratio %u\n",
           U, naive_max, f->n_erase, mn, mx, naive_max / mx);
    if (g_verbose) hist("log-structured append (10000 updates, 4 x 1 KB, 32 B records)", f, 4);
    /* C-3 재부팅 뒤 마운트: 최신 레코드와 쓰기 위치 */
    {
        log_t m;
        uint32_t seq = 0;
        CHECK(log_mount(&m, f, 0, 4, &seq, out));
        pay_of(U, d);
        CHECK(seq == U && memcmp(out, d, LOG_PAY) == 0);
        CHECK(m.cur == lg.cur && m.slot == lg.slot);
        pay_of(U + 1, d);
        CHECK(log_append(&m, d) == FLASH_OK && m.seq == U + 1);
        if (g_verbose) printf("  boot: mounted seq %u at sector %u slot %u, next append ok\n", seq, m.cur, m.slot - 1);
    }
    /* C-4 빈 플래시 / 쓰레기 플래시 → 마운트 실패 (포맷 필요) */
    {
        log_t m; uint32_t seq;
        CHECK(flash_init(f, 0x08000000u, NULL, 1024, 4, 4));
        CHECK(!log_mount(&m, f, 0, 4, &seq, NULL));
        fill_raw(f, 0, 4096, 77);
        CHECK(!log_mount(&m, f, 0, 4, &seq, NULL));
    }
    /* C-5 전원 차단 300 회: 마운트 = 마지막 ack 또는 그 +1, 이어서 100 개 더 써도 오류 없음 */
    {
        uint32_t fails = 0, plus1 = 0, torn_slot_skips = 0;
        for (uint32_t t = 1; t <= 300; t++) {
            flash_init(f, 0x08000000u, NULL, 1024, 4, 4);
            f->rng = t * 7919u;
            log_t w;
            if (log_format(&w, f, 0, 4) != FLASH_OK) fails++;
            uint32_t pre = 1 + rnd() % 400;
            for (uint32_t i = 1; i <= pre; i++) { pay_of(i, d); if (log_append(&w, d) != FLASH_OK) fails++; }
            f->cut_at = f->op_index + 1 + rnd() % 60;                /* 다음 몇 개 append 안 어딘가 */
            uint32_t acked = w.seq;
            for (int i = 0; i < 40; i++) {
                pay_of(w.seq + 1, d);
                if (log_append(&w, d) != FLASH_OK) break;
                acked = w.seq;
            }
            flash_power_on(f);
            log_t m; uint32_t seq = 0;
            if (!log_mount(&m, f, 0, 4, &seq, out)) { fails++; continue; }
            pay_of(seq, d);
            if (!(seq == acked || seq == acked + 1) || memcmp(out, d, LOG_PAY) != 0) fails++;
            plus1 += seq == acked + 1;
            uint8_t raw[LOG_REC];
            if (m.slot > 0) {                                        /* 마지막 비-blank 슬롯이 유효하지 않으면 건너뛴 것 */
                uint32_t s2;
                flash_read(f, log_slot_addr(&m, m.cur, m.slot - 1), raw, LOG_REC);
                torn_slot_skips += !log_unpack(raw, &s2, NULL);
            }
            for (uint32_t i = 1; i <= 100; i++) {                    /* 재부팅 뒤엔 새 내용 (같은 내용 재시도 아님) */
                pay_of(m.seq + 1 + 1000000u, d);
                if (log_append(&m, d) != FLASH_OK) fails++;
            }
            log_t m2; uint32_t seq2 = 0;
            if (!log_mount(&m2, f, 0, 4, &seq2, NULL) || seq2 != seq + 100) fails++;
        }
        CHECK(fails == 0);
        CHECK(torn_slot_skips > 0);
        printf("Part C: 300 power cuts | mount = last ack (+1 when the torn write completed: %u) | torn slots skipped %u\n",
               plus1, torn_slot_skips);
    }
}

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
