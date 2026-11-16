/* BM-A05 시리얼 부트로더: YMODEM 수신과 플래시 기록 — 시작 코드 (TODO A1–A4, C1–C2)
 *
 * Part A — YMODEM 수신 핵심: CRC-16/XMODEM, 패킷 읽기(SOH 128 / STX 1024, 블록 번호 + 보수, CRC),
 *          block 0 (파일 이름 + 크기) 파싱, 수신 상태 머신 ('C' 시작, ACK/NAK, EOT→NAK→EOT→ACK, null block 0).
 * Part B — 고장 주입: 비트 플립, 바이트 유실, 중복 블록, ACK 유실, 송신기 침묵, 중간 CAN, 틀린 블록 번호,
 *          크기 거짓말, 나쁜 이미지, 플래시 verify 실패, 고정 시드 랜덤 soak.
 * Part C — 플래시 기록 경로: 섹터는 처음 닿을 때 지우기(lazy erase), 블록마다 blank check → program → verify,
 *          헤더 64 B 는 RAM 에 들고 있다가 payload CRC 확인 뒤 마지막에 기록(commit), 모든 플래시 연산 지점
 *          전원 차단 → 다음 부팅에서 부트로더가 이미지를 거부 → 재전송으로 복구.
 *
 * 시간 단위: 1 tick = 115200 8N1 에서 1 바이트 시간(86.8 µs). 11520 tick = 1 s.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st      (TODO 를 채우기 전엔 FAIL, exit 1)
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================ (BM-A02 플래시 모델 축약판) ============================ */
#define FLASH_MAX (128u * 1024u)
typedef struct {
    uint32_t base;            /* 0x08000000 */
    uint32_t sector_size;     /* 축약판: 균일 섹터 */
    uint32_t n_sectors;
    uint32_t prog_unit;       /* 한 번에 프로그램하는 바이트 (주소·길이 정렬 단위) */
    uint8_t  mem[FLASH_MAX];  /* 지워진 상태 = 0xFF, program 은 mem &= data (1→0 만) */
    uint32_t n_erase, n_prog; /* 연산 횟수 (마모, 비용) */
    uint32_t op_index;        /* erase/program 호출마다 +1 — 전원 차단 훅 */
    uint32_t cut_at;          /* 0 = 안 끊음. op_index == cut_at 인 연산이 찢기고 FLASH_POWER_CUT */
    uint32_t rng;             /* 찢긴 연산의 미정 비트용 xorshift32 */
    uint32_t stuck1_addr;     /* (A05 추가) 0 으로 안 써지는 셀 주소 — verify 고장 주입 */
    uint8_t  stuck1_mask;     /* (A05 추가) 그 바이트에서 1 로 붙은 비트, 0 = 고장 없음 */
} flash_t;
typedef enum { FLASH_OK = 0, FLASH_ERR_ALIGN, FLASH_ERR_RANGE, FLASH_ERR_NOT_ERASED, FLASH_ERR_VERIFY,
               FLASH_POWER_CUT } flash_rc_t;

static uint32_t fl_rand(uint32_t *s) { uint32_t x = *s ? *s : 1u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static bool flash_dead(const flash_t *f) { return f->cut_at != 0 && f->op_index >= f->cut_at; }

flash_rc_t flash_erase_sector(flash_t *f, uint32_t sector) {
    if (flash_dead(f)) return FLASH_POWER_CUT;           /* 전원이 나간 뒤에는 아무것도 안 된다 */
    if (sector >= f->n_sectors) return FLASH_ERR_RANGE;
    f->op_index++; f->n_erase++;
    uint8_t *p = f->mem + sector * f->sector_size;
    if (f->cut_at && f->op_index == f->cut_at) {         /* 찢긴 erase: 0xFF / 옛 값 / 무작위가 섞인다 */
        for (uint32_t i = 0; i < f->sector_size; i++) {
            uint32_t r = fl_rand(&f->rng);
            if (r % 3u == 0) p[i] = 0xFF; else if (r % 3u == 2) p[i] = (uint8_t)(r >> 8);
        }
        return FLASH_POWER_CUT;
    }
    memset(p, 0xFF, f->sector_size);
    return FLASH_OK;
}

flash_rc_t flash_program(flash_t *f, uint32_t addr, const uint8_t *src, uint32_t len) {
    if (flash_dead(f)) return FLASH_POWER_CUT;
    uint32_t total = f->sector_size * f->n_sectors;
    if (addr < f->base || len > total || addr - f->base > total - len) return FLASH_ERR_RANGE;
    if ((addr - f->base) % f->prog_unit || len % f->prog_unit) return FLASH_ERR_ALIGN;
    f->op_index++; f->n_prog++;
    uint8_t *p = f->mem + (addr - f->base);
    uint32_t units = len / f->prog_unit, torn = UINT32_MAX;
    if (f->cut_at && f->op_index == f->cut_at) torn = units ? fl_rand(&f->rng) % units : 0u;
    for (uint32_t u = 0; u < units && (torn == UINT32_MAX || u <= torn); u++)
        for (uint32_t i = 0; i < f->prog_unit; i++) {
            uint32_t k = u * f->prog_unit + i;
            if (u == torn) {                               /* 찢긴 단위: 1→0 전이 중 무작위 일부만 */
                uint8_t trans = (uint8_t)(p[k] & (uint8_t)~src[k]);
                p[k] = (uint8_t)(p[k] & (uint8_t)~(trans & (uint8_t)fl_rand(&f->rng)));
            } else {
                p[k] &= src[k];                            /* AND 의미론 */
            }
        }
    if (f->stuck1_mask && f->stuck1_addr >= addr && f->stuck1_addr - addr < len)
        f->mem[f->stuck1_addr - f->base] |= f->stuck1_mask;
    return torn != UINT32_MAX ? FLASH_POWER_CUT : FLASH_OK;
}

void flash_read(const flash_t *f, uint32_t addr, uint8_t *dst, uint32_t len) {
    uint32_t total = f->sector_size * f->n_sectors;
    if (addr < f->base || len > total || addr - f->base > total - len) { memset(dst, 0, len); return; }
    memcpy(dst, f->mem + (addr - f->base), len);
}
void flash_power_on(flash_t *f) { f->cut_at = 0; }        /* "재부팅": 끊김 훅 해제 */

/* ============================ (BM-A04 헤더 축약판) — CRC-32 만, SHA 생략 ============================ */
#define IMG_MAGIC     0x474D4942u  /* 메모리 바이트 'B','I','M','G' */
#define IMG_HDR_SIZE  64u
typedef struct {
    uint32_t magic;
    uint16_t hdr_version;    /* 1 */
    uint16_t hdr_size;       /* 64 */
    uint32_t img_version;    /* major<<24 | minor<<16 | patch */
    uint32_t payload_size;
    uint32_t load_addr;
    uint32_t flags;          /* 축약판: bit0(SHA256) = 0 */
    uint32_t payload_crc32;  /* CRC-32/ISO-HDLC (zlib) */
    uint8_t  sha256[32];     /* 축약판: 0 */
    uint32_t hdr_crc32;      /* CRC-32 of bytes [0, 60) */
} img_hdr_t;
_Static_assert(sizeof(img_hdr_t) == 64, "header must be 64 bytes");
typedef enum { IMG_OK = 0, IMG_NO_IMAGE, IMG_BAD_MAGIC, IMG_BAD_HDR_CRC, IMG_BAD_HDR_VERSION, IMG_BAD_SIZE,
               IMG_BAD_LOAD_ADDR, IMG_BAD_CRC } img_rc_t;
static const char *img_rc_name(img_rc_t r) {
    static const char *n[] = {"OK", "NO_IMAGE", "BAD_MAGIC", "BAD_HDR_CRC", "BAD_HDR_VERSION", "BAD_SIZE",
                              "BAD_LOAD_ADDR", "BAD_CRC"};
    return (unsigned)r < 8u ? n[r] : "?";
}
static uint32_t crc32_tab[256];
uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n) {   /* zlib 식 체이닝: 처음 crc = 0 */
    if (!crc32_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            crc32_tab[i] = c;
        }
    crc = ~crc;
    while (n--) crc = crc32_tab[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

void hdr_encode(img_hdr_t *h, uint8_t b[IMG_HDR_SIZE]) {      /* 리틀 엔디언 직렬화 + hdr_crc32 채움 */
    memset(b, 0, IMG_HDR_SIZE);
    wr32(b + 0, h->magic); wr16(b + 4, h->hdr_version); wr16(b + 6, h->hdr_size);
    wr32(b + 8, h->img_version); wr32(b + 12, h->payload_size); wr32(b + 16, h->load_addr);
    wr32(b + 20, h->flags); wr32(b + 24, h->payload_crc32); memcpy(b + 28, h->sha256, 32);
    h->hdr_crc32 = crc32_update(0, b, 60); wr32(b + 60, h->hdr_crc32);
}
/* 헤더 64 B 만 보는 검사 (payload CRC 는 img_check). 정수 넘침 안전한 크기 비교. */
img_rc_t img_hdr_check(const uint8_t b[IMG_HDR_SIZE], uint32_t slot_addr, uint32_t slot_len, img_hdr_t *h) {
    uint32_t ff = 0;
    for (uint32_t i = 0; i < IMG_HDR_SIZE; i++) ff += b[i] == 0xFF;
    if (ff == IMG_HDR_SIZE) return IMG_NO_IMAGE;                     /* 지워진 채 = 이미지 없음 */
    h->magic = rd32(b); h->hdr_version = rd16(b + 4); h->hdr_size = rd16(b + 6);
    h->img_version = rd32(b + 8); h->payload_size = rd32(b + 12); h->load_addr = rd32(b + 16);
    h->flags = rd32(b + 20); h->payload_crc32 = rd32(b + 24); memcpy(h->sha256, b + 28, 32); h->hdr_crc32 = rd32(b + 60);
    if (h->magic != IMG_MAGIC) return IMG_BAD_MAGIC;
    if (crc32_update(0, b, 60) != h->hdr_crc32) return IMG_BAD_HDR_CRC;
    if (h->hdr_version != 1 || h->hdr_size != IMG_HDR_SIZE) return IMG_BAD_HDR_VERSION;
    if (slot_len < IMG_HDR_SIZE || h->payload_size > slot_len - IMG_HDR_SIZE) return IMG_BAD_SIZE;
    if (h->load_addr != slot_addr + IMG_HDR_SIZE) return IMG_BAD_LOAD_ADDR;
    return IMG_OK;
}
img_rc_t img_check(const flash_t *f, uint32_t slot_addr, uint32_t slot_len, img_hdr_t *h) {
    uint8_t b[256];
    flash_read(f, slot_addr, b, IMG_HDR_SIZE);
    img_rc_t r = img_hdr_check(b, slot_addr, slot_len, h);
    if (r != IMG_OK) return r;
    uint32_t crc = 0;
    for (uint32_t o = 0; o < h->payload_size; o += sizeof b) {
        uint32_t k = h->payload_size - o < sizeof b ? h->payload_size - o : (uint32_t)sizeof b;
        flash_read(f, slot_addr + IMG_HDR_SIZE + o, b, k);
        crc = crc32_update(crc, b, k);
    }
    return crc == h->payload_crc32 ? IMG_OK : IMG_BAD_CRC;
}

/* ============================ 보드 배치 / 플랫폼 이음매 ============================ */
#define FL_BASE        0x08000000u
#define FL_SECTOR      16384u          /* 축약판: 16 KB 균일 (F4 의 섹터 0–3 크기) */
#define FL_NSECT       8u              /* 128 KB */
#define FL_PROG_UNIT   8u
#define SLOT_FIRST_SECTOR 2u           /* 섹터 0–1 = 부트로더 (32 KB) */
#define SLOT_ADDR      (FL_BASE + SLOT_FIRST_SECTOR * FL_SECTOR)   /* 0x08008000 */
#define SLOT_SIZE      ((FL_NSECT - SLOT_FIRST_SECTOR) * FL_SECTOR) /* 98304 */
#define TICKS_PER_S    11520u          /* 1 tick = 1 바이트 시간 @115200 8N1 */

int  uart_getc(uint32_t timeout_ticks);    /* 받은 바이트(0..255) 또는 -1 (timeout) */
void uart_putc(uint8_t b);
void plat_busy(uint32_t ticks);            /* 플래시 BSY 대기 동안 시간이 흐른다 (그동안 UART 는 계속 들어온다) */

typedef struct { uint32_t erase_ticks, prog_ticks_per_kb; } fl_timing_t;
static fl_timing_t g_fl_time = {4608u, 47u};  /* 16 KB erase 400 ms, 1 KB program ≈ 4.1 ms (대표값 가정) */

static flash_rc_t fl_erase(flash_t *f, uint32_t sector) {              /* HAL 식: 시작 + BSY 대기 */
    flash_rc_t r = flash_erase_sector(f, sector);
    plat_busy(g_fl_time.erase_ticks);
    return r;
}
static flash_rc_t fl_program(flash_t *f, uint32_t addr, const uint8_t *p, uint32_t n) {
    flash_rc_t r = flash_program(f, addr, p, n);
    plat_busy((n * g_fl_time.prog_ticks_per_kb + 1023u) / 1024u);
    return r;
}

/* ============================ Part A1: CRC-16/XMODEM ============================ */
/* poly 0x1021, init 0x0000, 반사 없음, xorout 0. "123456789" → 0x31C3 */
uint16_t crc16_xmodem(const uint8_t *p, uint32_t n) {
    /* TODO A1: MSB-first 비트 단위 CRC. crc ^= byte << 8, 8 번 shift, bit15 가 1 이면 ^ 0x1021 */
    (void)p; (void)n;
    return 0;
}

/* ============================ YMODEM 상수 / 타입 ============================ */
#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define CRCC 'C'
#define CPMEOF 0x1A
#define YM_CHAR_TO     (1u * TICKS_PER_S)    /* 패킷 안 글자 사이 1 s */
#define YM_PKT_TO      (10u * TICKS_PER_S)   /* 다음 패킷 시작까지 10 s */
#define YM_START_TO    (3u * TICKS_PER_S)    /* 'C' 재전송 간격 3 s */
#define YM_START_TRIES 20u                   /* 60 s 동안 송신기 없으면 포기 */
#define YM_MAX_ERRS    10u                   /* 연속 오류 10 회 → CAN */
#define YM_PURGE_MAX   4096u

typedef enum { PKT_OK = 0, PKT_EOT, PKT_CAN, PKT_TIMEOUT, PKT_SHORT, PKT_GARBAGE, PKT_BAD_BLK, PKT_BAD_CRC } pkt_rc_t;
typedef enum { YM_OK = 0, YM_ERR_NO_SENDER, YM_ERR_TIMEOUT, YM_ERR_CANCELLED, YM_ERR_SEQ, YM_ERR_BLOCK0,
               YM_ERR_TOO_BIG, YM_ERR_SHORT, YM_ERR_IMAGE, YM_ERR_FLASH, YM_ERR_POWER } ym_rc_t;
static const char *ym_rc_name(ym_rc_t r) {
    static const char *n[] = {"OK", "NO_SENDER", "TIMEOUT", "CANCELLED", "SEQ", "BLOCK0", "TOO_BIG", "SHORT",
                              "IMAGE", "FLASH", "POWER"};
    return (unsigned)r < 11u ? n[r] : "?";
}
typedef struct { uint32_t c_sent, acks, naks, cans, blocks, dups, crc_errs, blk_errs, garbage, shorts, timeouts,
                 purged, eots; } ym_stats_t;

/* ============================ 플래시 싱크 (수신 데이터 → 슬롯) ============================ */
typedef enum { SINK_OK = 0, SINK_ERR_SIZE, SINK_ERR_IMAGE, SINK_ERR_FLASH, SINK_ERR_VERIFY, SINK_ERR_POWER } sink_rc_t;
typedef enum { ERASE_LAZY = 0, ERASE_UPFRONT_NEEDED, ERASE_UPFRONT_SLOT } erase_policy_t;
typedef struct {
    flash_t *f;
    uint32_t slot_addr, slot_size;
    erase_policy_t policy;
    uint32_t size;                 /* block 0 의 파일 크기 */
    uint32_t erased;               /* bit i = 슬롯 섹터 i 를 이번 세션에 지웠다 */
    uint8_t  hdr[IMG_HDR_SIZE];    /* 헤더는 RAM 에만 — commit 때 마지막에 기록 */
    uint32_t hdr_have;
    bool     hdr_valid;
    img_hdr_t h;
    uint32_t committed;            /* program + verify 까지 끝난 파일 오프셋 (마지막 good 지점) */
    flash_rc_t last_frc;
} sink_t;

static sink_rc_t sink_map(flash_rc_t r) {
    return r == FLASH_OK ? SINK_OK : r == FLASH_POWER_CUT ? SINK_ERR_POWER : SINK_ERR_FLASH;
}
/* 슬롯 섹터 i 를 (이번 세션에 아직 안 지웠으면) 지운다 */
static sink_rc_t sink_erase_sector(sink_t *s, uint32_t i) {
    if (s->erased >> i & 1u) return SINK_OK;
    flash_rc_t r = fl_erase(s->f, (s->slot_addr - s->f->base) / s->f->sector_size + i);
    s->last_frc = r;
    if (r != FLASH_OK) return sink_map(r);
    s->erased |= 1u << i;
    return SINK_OK;
}
void sink_init(sink_t *s, flash_t *f, uint32_t slot_addr, uint32_t slot_size, erase_policy_t pol) {
    memset(s, 0, sizeof *s);
    s->f = f; s->slot_addr = slot_addr; s->slot_size = slot_size; s->policy = pol;
}
/* block 0 을 받은 직후: 크기 검사 + (정책이 upfront 면) 미리 지우기 */
sink_rc_t sink_begin(sink_t *s, uint32_t size) {
    if (size <= IMG_HDR_SIZE || size > s->slot_size) return SINK_ERR_SIZE;
    s->size = size; s->erased = 0; s->hdr_have = 0; s->hdr_valid = false; s->committed = 0;
    if (s->policy != ERASE_LAZY) {
        uint32_t n = s->policy == ERASE_UPFRONT_SLOT ? s->slot_size / s->f->sector_size
                                                     : (size - 1u) / s->f->sector_size + 1u;
        for (uint32_t i = 0; i < n; i++) { sink_rc_t r = sink_erase_sector(s, i); if (r) return r; }
    }
    return SINK_OK;
}

/* ============================ Part C1: sink_write ============================ */
/* 파일 오프셋 off 부터 n 바이트 (YMODEM 블록, 이미 size 로 잘림). off 는 prog_unit 정렬.
 * [0,64) 은 RAM 에 모으고 다 모이면 헤더를 미리 검사(fail fast), 나머지는
 * lazy erase → 0xFF 패딩 → blank check → program → read-back verify. */
sink_rc_t sink_write(sink_t *s, uint32_t off, const uint8_t *d, uint32_t n) {
    /* TODO C1: (1) [0,64) 은 s->hdr 로 복사, 다 모이면 img_hdr_check + payload_size == size - 64 (아니면 IMAGE)
     *          (2) 닿는 슬롯 섹터를 sink_erase_sector 로 lazy erase  (3) 꼬리 0xFF 패딩 (prog_unit)
     *          (4) blank check (old & new) == new  (5) fl_program  (6) read-back verify → committed 갱신 */
    (void)s; (void)off; (void)d; (void)n; (void)fl_program;
    return SINK_ERR_FLASH;
}

/* ============================ Part C2: sink_commit ============================ */
/* 두 번째 EOT 에서 호출. 플래시에서 다시 읽은 payload 의 CRC-32 가 헤더와 맞을 때만 헤더를 기록한다.
 * 헤더가 슬롯의 "유효" 표시 — 이것이 마지막 쓰기라서, 그 전 어디서 끊겨도 슬롯은 무효(헤더 = 0xFF). */
sink_rc_t sink_commit(sink_t *s) {
    /* TODO C2: 헤더 유효 + committed == size 확인 → 플래시에서 다시 읽은 payload CRC-32 == 헤더 값
     *          → 헤더 영역 blank 확인 → 헤더 64 B program (마지막 쓰기) → verify → img_check == IMG_OK */
    (void)s;
    return SINK_ERR_IMAGE;
}

/* ============================ YMODEM 수신기 ============================ */
typedef struct {
    sink_t    *sink;
    char       name[64];
    uint32_t   size;      /* block 0 의 크기 */
    uint32_t   written;   /* 싱크에 넘긴 파일 바이트 */
    ym_stats_t st;
} ym_t;

static void ym_send(ym_t *y, uint8_t b) {
    uart_putc(b);
    if (b == CRCC) y->st.c_sent++; else if (b == ACK) y->st.acks++; else if (b == NAK) y->st.naks++;
    else if (b == CAN) y->st.cans++;
}
static void ym_cancel(ym_t *y) { ym_send(y, CAN); ym_send(y, CAN); }
/* 오류 뒤: 선이 1 s 조용해질 때까지 읽어 버린다 (패킷 나머지 / 쓰레기) */
static void ym_purge(ym_t *y) {
    for (uint32_t i = 0; i < YM_PURGE_MAX; i++) {
        if (uart_getc(YM_CHAR_TO) < 0) return;
        y->st.purged++;
    }
}

/* ============================ Part A2: 패킷 하나 읽기 ============================ */
/* 첫 바이트는 first_to, 나머지는 글자마다 YM_CHAR_TO. 성공이면 buf/len/blk 채움. */
pkt_rc_t ym_read_packet(uint8_t *buf, uint32_t *len, uint8_t *blk, uint32_t first_to) {
    /* TODO A2: 첫 바이트(first_to): EOT / CAN+CAN / SOH(128) / STX(1024) / 그 밖 = GARBAGE.
     *          blk, ~blk, data, CRC hi, lo 는 글자마다 YM_CHAR_TO (없으면 SHORT). 보수 → BAD_BLK, CRC → BAD_CRC */
    (void)buf; (void)len; (void)blk; (void)first_to;
    return PKT_TIMEOUT;
}

/* ============================ Part A3: block 0 파싱 ============================ */
/* "name\0<size decimal>[ <mtime octal> <mode octal> ...]\0..." → 0 OK, 1 null 경로(배치 끝), -1 형식 오류.
 * 부트로더는 크기를 필수로 요구한다 (패딩 0x1A 를 잘라 내는 유일한 근거). */
int ym_parse_block0(const uint8_t *d, uint32_t len, char *name, uint32_t cap, uint32_t *size) {
    /* TODO A3: NUL 찾기 (없으면 -1, 길이 0 이면 1), 이름 복사 (cap 확인), 10 진 크기 (숫자 ≥ 1 개, 32 비트 넘침 -1,
     *          뒤는 ' ' 또는 NUL) */
    (void)d; (void)len; (void)name; (void)cap; (void)size;
    return -1;
}

/* ============================ Part A4: 수신 상태 머신 ============================ */
ym_rc_t ym_receive(ym_t *y) {
    /* TODO A4: 'C' 로 시작 → block 0 (파싱, sink_begin, ACK + 'C') → 데이터 블록 (expect 와 같으면 잘라서
     *          sink_write + ACK, expect-1 이면 중복 ACK, 그 밖은 CAN) → EOT: 첫 번째 NAK, 두 번째 commit + ACK + 'C'
     *          → null block 0 ACK. 오류는 purge + NAK, 연속 YM_MAX_ERRS 초과면 CAN. 전원 차단이면 즉시 반환. */
    (void)ym_cancel; (void)ym_purge;
    memset(&y->st, 0, sizeof y->st);
    ym_send(y, CRCC);
    return YM_ERR_NO_SENDER;
}

/* ============================ 부트로더 글루 (Part C 시나리오) ============================ */
static char g_log[8192];
static uint32_t g_log_n;
static void blog(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int k = vsnprintf(g_log + g_log_n, sizeof g_log - g_log_n, fmt, ap);
    va_end(ap);
    if (k > 0) g_log_n += (uint32_t)k < sizeof g_log - g_log_n ? (uint32_t)k : (uint32_t)(sizeof g_log - g_log_n - 1u);
    if (g_log_n < sizeof g_log - 1u) g_log[g_log_n++] = '\n';
    g_log[g_log_n] = '\0';
}
uint64_t sim_now(void);
typedef enum { BOOT_APP = 0, BOOT_STAY } boot_rc_t;
boot_rc_t boot_main(flash_t *f, ym_t *y, sink_t *s, ym_rc_t *yr_out) {
    img_hdr_t h;
    img_rc_t r = img_check(f, SLOT_ADDR, SLOT_SIZE, &h);
    blog("[boot] slot@0x%08X check: %s", (unsigned)SLOT_ADDR, img_rc_name(r));
    if (r == IMG_OK) {
        blog("[boot] app v%u.%u.%u payload %u B crc32=%08X -> jump 0x%08X (BM-A03)", (unsigned)(h.img_version >> 24),
             (unsigned)(h.img_version >> 16 & 0xFFu), (unsigned)(h.img_version & 0xFFFFu), (unsigned)h.payload_size,
             (unsigned)h.payload_crc32, (unsigned)h.load_addr);
        return BOOT_APP;
    }
    blog("[boot] no valid app -> YMODEM recovery on UART ('C' every 3 s)");
    uint32_t e0 = f->n_erase, p0 = f->n_prog; uint64_t t0 = sim_now();
    sink_init(s, f, SLOT_ADDR, SLOT_SIZE, ERASE_LAZY);
    y->sink = s;
    ym_rc_t yr = ym_receive(y);
    if (yr_out) *yr_out = yr;
    blog("[ym] rc=%s file='%s' size=%u blocks=%u dups=%u naks=%u crc_err=%u short=%u timeouts=%u", ym_rc_name(yr),
         y->name, (unsigned)y->size, (unsigned)y->st.blocks, (unsigned)y->st.dups, (unsigned)y->st.naks,
         (unsigned)y->st.crc_errs, (unsigned)y->st.shorts, (unsigned)y->st.timeouts);
    blog("[ym] flash: erases=%u programs=%u committed=%u B, elapsed %llu ms", (unsigned)(f->n_erase - e0),
         (unsigned)(f->n_prog - p0), (unsigned)s->committed,
         (unsigned long long)((sim_now() - t0) * 1000u / TICKS_PER_S));
    if (yr == YM_ERR_POWER) { blog("[sim] *** power lost ***"); return BOOT_STAY; }
    if (yr != YM_OK) { blog("[boot] update failed (%s) -> stay in bootloader", ym_rc_name(yr)); return BOOT_STAY; }
    r = img_check(f, SLOT_ADDR, SLOT_SIZE, &h);
    blog("[boot] reset -> slot check: %s", img_rc_name(r));
    return r == IMG_OK ? BOOT_APP : BOOT_STAY;
}

/* ============================ 시뮬레이터: 시간 + UART + 채널 + 송신기 모델 ============================ */
#define RXQ_MAX     8192u
#define SND_TO      (10u * TICKS_PER_S)     /* 송신기 ACK 타임아웃 10 s (모델 값) */
#define SND_RETRIES 10
#define EVENT_CAP   50000000ull
#define T_INF       UINT64_MAX
typedef struct {
    uint32_t seed;
    uint32_t p_flip_s2r, p_drop_s2r, p_drop_r2s, p_flip_r2s;  /* 바이트당 ppm */
    int32_t  flip_s2r_at, drop_s2r_at, drop_r2s_at;           /* 스트림 바이트 번호, -1 = 없음 */
    int32_t  cancel_at_blk, die_at_blk, skip_seq_at_blk, dup_blk, eot_at_blk;
    uint32_t lie_size;                                       /* block 0 에 실제보다 크게 */
    uint64_t start_delay;                                    /* 송신기가 늦게 시작 (tick) */
    uint32_t turnaround;                                     /* 송신기 응답 지연 (USB-UART 지연 등) */
    bool     only_128;
} fault_t;
static fault_t fault_none(void) {
    fault_t f; memset(&f, 0, sizeof f);
    f.seed = 1; f.flip_s2r_at = f.drop_s2r_at = f.drop_r2s_at = -1;
    f.cancel_at_blk = f.die_at_blk = f.skip_seq_at_blk = f.dup_blk = f.eot_at_blk = -1;
    return f;
}
enum { SN_WAIT_C0, SN_IDLE, SN_TX, SN_WAIT_ACK0, SN_WAIT_C1, SN_WAIT_ACK, SN_WAIT_EOT1, SN_WAIT_EOT2, SN_WAIT_CF,
       SN_WAIT_ACKF, SN_RAW, SN_DONE, SN_ABORT, SN_GAVEUP, SN_DEAD };
typedef struct {
    uint64_t now, events; bool halted;
    uint8_t  rxq[RXQ_MAX]; uint32_t rh, rt, rxq_depth, overruns;
    uint32_t s2r_n, r2s_n, crng;
    uint64_t last_pop_t, max_stall;           /* 수신기가 마지막 바이트를 읽은 뒤 → ACK 까지 최대 지연 */
    fault_t  fx;
    uint8_t  resp[8192]; uint32_t resp_n;     /* 수신기가 보낸 바이트 (채널 고장 전) */
    int st, after; uint64_t t_next;           /* ---- 송신기 ---- */
    const uint8_t *file; uint32_t fsize; const char *fname;
    uint8_t  pkt[1029]; uint32_t plen, ppos;
    uint32_t off, blen, blk; int retries, cans; bool dup_done;
    uint32_t pkts, retx, s_timeouts;
    const uint8_t *raw; uint32_t raw_n;
} sim_t;
static sim_t S;
uint64_t sim_now(void) { return S.now; }
static uint32_t crand(void) { return fl_rand(&S.crng); }
static bool ppm(uint32_t p) { return p && crand() % 1000000u < p; }

static void s2r(uint8_t b) {                       /* 송신기 → (채널) → 수신기 RX FIFO */
    uint32_t i = S.s2r_n++;
    if ((int32_t)i == S.fx.drop_s2r_at || ppm(S.fx.p_drop_s2r)) return;
    if ((int32_t)i == S.fx.flip_s2r_at || ppm(S.fx.p_flip_s2r)) b ^= (uint8_t)(1u << (crand() % 8u));
    if (S.rt - S.rh >= S.rxq_depth) { S.overruns++; return; }
    S.rxq[S.rt++ % RXQ_MAX] = b;
}
static void snd_tx(int after) { S.ppos = 0; S.st = SN_TX; S.after = after; S.t_next = S.now + 1u + S.fx.turnaround; S.pkts++; }
static void snd_crc(uint32_t n) { uint16_t c = crc16_xmodem(S.pkt + 3, n); S.pkt[3 + n] = (uint8_t)(c >> 8); S.pkt[4 + n] = (uint8_t)c; S.plen = n + 5u; }
static void snd_b0(bool null_blk) {
    memset(S.pkt, 0, sizeof S.pkt);
    S.pkt[0] = SOH; S.pkt[1] = 0; S.pkt[2] = 0xFF;
    if (!null_blk) snprintf((char *)S.pkt + 3, 125, "%s%c%u 15071360000 100644", S.fname, 0,
                            (unsigned)(S.fsize + S.fx.lie_size));
    snd_crc(128);
}
static void snd_eot(int after) { S.pkt[0] = EOT; S.plen = 1; snd_tx(after); }
static void snd_next_data(void) {
    if (S.off >= S.fsize || S.fx.eot_at_blk == (int32_t)S.blk) { snd_eot(SN_WAIT_EOT1); return; }
    if (S.fx.die_at_blk == (int32_t)S.blk) { S.st = SN_DEAD; S.t_next = T_INF; return; }
    if (S.fx.cancel_at_blk == (int32_t)S.blk) { S.pkt[0] = S.pkt[1] = CAN; S.plen = 2; snd_tx(SN_ABORT); return; }
    uint32_t rem = S.fsize - S.off;
    S.blen = (!S.fx.only_128 && rem >= 1024u) ? 1024u : 128u;
    uint8_t seq = (uint8_t)(S.blk + (S.fx.skip_seq_at_blk == (int32_t)S.blk ? 1u : 0u));
    S.pkt[0] = S.blen == 1024u ? STX : SOH; S.pkt[1] = seq; S.pkt[2] = (uint8_t)~seq;
    uint32_t k = rem < S.blen ? rem : S.blen;
    memcpy(S.pkt + 3, S.file + S.off, k); memset(S.pkt + 3 + k, CPMEOF, S.blen - k);
    snd_crc(S.blen);
    snd_tx(SN_WAIT_ACK);
}
static void snd_wait(int st) { S.st = st; S.t_next = S.now + SND_TO; }
static void snd_rx(uint8_t b) {                    /* 수신기 → 송신기 바이트 */
    if (S.st == SN_TX || S.st == SN_IDLE || S.st >= SN_RAW) return;   /* 보내는 중엔 안 읽는다 */
    if (b == CAN) { if (++S.cans >= 2) { S.st = SN_ABORT; S.t_next = T_INF; } return; }
    S.cans = 0;
    switch (S.st) {
    case SN_WAIT_C0:   if (b == CRCC) { snd_b0(false); snd_tx(SN_WAIT_ACK0); } break;
    case SN_WAIT_ACK0: if (b == ACK) { S.retries = 0; snd_wait(SN_WAIT_C1); } else if (b == NAK) { S.retx++; snd_tx(SN_WAIT_ACK0); } break;
    case SN_WAIT_C1:   if (b == CRCC) { S.retries = 0; S.off = 0; S.blk = 1; snd_next_data(); } break;
    case SN_WAIT_ACK:
        if (b == ACK) {
            S.retries = 0;
            if (S.fx.dup_blk == (int32_t)S.blk && !S.dup_done) { S.dup_done = true; S.retx++; snd_tx(SN_WAIT_ACK); break; }
            S.off += S.blen; S.blk++; snd_next_data();
        } else if (b == NAK) { S.retx++; snd_tx(SN_WAIT_ACK); }
        break;
    case SN_WAIT_EOT1: if (b == NAK) snd_eot(SN_WAIT_EOT2); else if (b == ACK) { S.retries = 0; snd_wait(SN_WAIT_CF); } break;
    case SN_WAIT_EOT2: if (b == ACK) { S.retries = 0; snd_wait(SN_WAIT_CF); } else if (b == NAK) snd_eot(SN_WAIT_EOT2); break;
    case SN_WAIT_CF:   if (b == CRCC) { snd_b0(true); snd_tx(SN_WAIT_ACKF); } break;
    case SN_WAIT_ACKF: if (b == ACK) { S.st = SN_DONE; S.t_next = T_INF; } else if (b == NAK) snd_tx(SN_WAIT_ACKF); break;
    default: break;
    }
}
static void snd_timeout(void) {
    if (S.st == SN_WAIT_C0) { S.st = SN_GAVEUP; S.t_next = T_INF; return; }
    S.s_timeouts++;
    if (++S.retries > SND_RETRIES) { S.pkt[0] = S.pkt[1] = CAN; S.plen = 2; snd_tx(SN_GAVEUP); return; }
    switch (S.st) {
    case SN_WAIT_ACK0: case SN_WAIT_C1: snd_b0(false); S.retx++; snd_tx(SN_WAIT_ACK0); break;
    case SN_WAIT_ACK:  S.retx++; snd_tx(SN_WAIT_ACK); break;
    case SN_WAIT_EOT1: snd_eot(SN_WAIT_EOT1); break;
    case SN_WAIT_EOT2: case SN_WAIT_CF: snd_eot(SN_WAIT_EOT2); break;
    case SN_WAIT_ACKF: snd_b0(true); snd_tx(SN_WAIT_ACKF); break;
    default: S.t_next = T_INF; break;
    }
}
static void snd_event(void) {
    if (++S.events > EVENT_CAP) { S.halted = true; S.t_next = T_INF; return; }
    switch (S.st) {
    case SN_IDLE: S.st = SN_WAIT_C0; S.t_next = S.now + 60u * TICKS_PER_S; break;
    case SN_TX:
        s2r(S.pkt[S.ppos++]);
        if (S.ppos < S.plen) { S.t_next = S.now + 1u; break; }
        S.st = S.after;
        S.t_next = (S.after == SN_ABORT || S.after == SN_GAVEUP) ? T_INF : S.now + SND_TO;
        break;
    case SN_RAW:
        s2r(S.raw[S.ppos++]);
        if (S.ppos < S.raw_n) S.t_next = S.now + 1u; else { S.st = SN_DONE; S.t_next = T_INF; }
        break;
    default: snd_timeout(); break;
    }
}
int uart_getc(uint32_t to) {
    uint64_t dl = S.now + to;
    for (;;) {
        if (S.rh != S.rt) { S.last_pop_t = S.now; return S.rxq[S.rh++ % RXQ_MAX]; }
        if (S.halted || S.now >= dl) { if (S.now < dl) S.now = dl; return -1; }
        if (S.t_next <= dl) { if (S.t_next > S.now) S.now = S.t_next; snd_event(); }
        else S.now = dl;
    }
}
void uart_putc(uint8_t b) {
    if (S.resp_n < sizeof S.resp) S.resp[S.resp_n++] = b;
    if (b == ACK && S.now - S.last_pop_t > S.max_stall) S.max_stall = S.now - S.last_pop_t;
    uint32_t i = S.r2s_n++;
    if ((int32_t)i == S.fx.drop_r2s_at || ppm(S.fx.p_drop_r2s)) return;
    if (ppm(S.fx.p_flip_r2s)) b ^= (uint8_t)(1u << (crand() % 8u));
    snd_rx(b);
}
void plat_busy(uint32_t ticks) {
    uint64_t target = S.now + ticks;
    while (!S.halted && S.t_next <= target) { if (S.t_next > S.now) S.now = S.t_next; snd_event(); }
    S.now = target;
}
static void sim_reset(const uint8_t *file, uint32_t n, const char *name, const fault_t *fx) {
    memset(&S, 0, sizeof S);
    S.fx = *fx; S.crng = fx->seed ? fx->seed : 1u; S.rxq_depth = 64;
    S.file = file; S.fsize = n; S.fname = name;
    S.st = fx->start_delay ? SN_IDLE : SN_WAIT_C0;
    S.t_next = fx->start_delay ? fx->start_delay : 60u * TICKS_PER_S;
}
static void sim_reset_raw(const uint8_t *stream, uint32_t n, uint32_t depth) {
    fault_t fx = fault_none();
    sim_reset(NULL, 0, "", &fx);
    S.raw = stream; S.raw_n = n; S.rxq_depth = depth < RXQ_MAX ? depth : RXQ_MAX;
    S.st = SN_RAW; S.t_next = n ? 1u : T_INF;
}

/* ============================ 테스트 헬퍼 ============================ */
static flash_t g_fl;
static sink_t  g_sink;
static ym_t    g_ym;

static void flash_init(flash_t *f, uint32_t rng) {
    memset(f, 0, sizeof *f);
    f->base = FL_BASE; f->sector_size = FL_SECTOR; f->n_sectors = FL_NSECT; f->prog_unit = FL_PROG_UNIT;
    memset(f->mem, 0xFF, sizeof f->mem);
    memset(f->mem, 0xB0, SLOT_ADDR - FL_BASE);                 /* 부트로더 영역: 절대 바뀌면 안 된다 */
    f->rng = rng;
}
static bool boot_region_intact(const flash_t *f) {
    for (uint32_t i = 0; i < SLOT_ADDR - FL_BASE; i++) if (f->mem[i] != 0xB0) return false;
    return true;
}
enum { MK_TAIL_1A = 1, MK_BAD_CRC = 2, MK_BAD_MAGIC = 4, MK_SIZE_MISMATCH = 8 };
/* 헤더 + payload. 반환 = 파일 길이 */
static uint32_t make_image(uint8_t *out, uint32_t payload, uint32_t ver, uint32_t seed, unsigned mk) {
    uint32_t r = seed ? seed : 1u;
    for (uint32_t i = 0; i < payload; i++) out[IMG_HDR_SIZE + i] = (uint8_t)(fl_rand(&r) >> 7);
    if (mk & MK_TAIL_1A) for (uint32_t i = payload > 5u ? payload - 5u : 0u; i < payload; i++) out[IMG_HDR_SIZE + i] = CPMEOF;
    img_hdr_t h; memset(&h, 0, sizeof h);
    h.magic = (mk & MK_BAD_MAGIC) ? 0x474D4943u : IMG_MAGIC; h.hdr_version = 1; h.hdr_size = IMG_HDR_SIZE;
    h.img_version = ver; h.payload_size = payload + ((mk & MK_SIZE_MISMATCH) ? 16u : 0u);
    h.load_addr = SLOT_ADDR + IMG_HDR_SIZE;
    h.payload_crc32 = crc32_update(0, out + IMG_HDR_SIZE, payload) ^ ((mk & MK_BAD_CRC) ? 1u : 0u);
    hdr_encode(&h, out);
    return IMG_HDR_SIZE + payload;
}
static bool slot_equals(const flash_t *f, const uint8_t *file, uint32_t n) {
    return memcmp(f->mem + (SLOT_ADDR - FL_BASE), file, n) == 0;
}
static bool slot_hdr_blank(const flash_t *f) {
    for (uint32_t i = 0; i < IMG_HDR_SIZE; i++) if (f->mem[SLOT_ADDR - FL_BASE + i] != 0xFF) return false;
    return true;
}
static img_rc_t slot_check(void) { img_hdr_t h; return img_check(&g_fl, SLOT_ADDR, SLOT_SIZE, &h); }
static ym_rc_t xfer(const uint8_t *file, uint32_t n, const fault_t *fx, erase_policy_t pol) {
    sim_reset(file, n, "app.bin", fx);
    sink_init(&g_sink, &g_fl, SLOT_ADDR, SLOT_SIZE, pol);
    memset(&g_ym, 0, sizeof g_ym);
    g_ym.sink = &g_sink;
    return ym_receive(&g_ym);
}
static uint32_t n_data_blocks(uint32_t n, bool only128) {
    uint32_t b = 0, off = 0;
    while (off < n) { uint32_t bl = (!only128 && n - off >= 1024u) ? 1024u : 128u; off += bl; b++; }
    return b;
}

#ifndef YM_NO_MAIN
/* ============================ 테스트 하네스 ============================ */
static int g_pass, g_total;
static bool g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint8_t g_img[SLOT_SIZE + 2048], g_img2[SLOT_SIZE + 2048];

static void test_part_a(void) {
    /* A1: CRC-16/XMODEM */
    CHECK(crc16_xmodem((const uint8_t *)"123456789", 9) == 0x31C3u);
    CHECK(crc16_xmodem((const uint8_t *)"A", 1) == 0x58E5u && crc16_xmodem(NULL, 0) == 0);
    uint32_t r = 0x1234567u, zero = 0;
    for (int t = 0; t < 2000; t++) {                     /* CRC 를 덧붙이면 나머지 0 (선형·init 0 성질) */
        uint8_t b[200]; uint32_t n = fl_rand(&r) % 198u;
        for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t)fl_rand(&r);
        uint16_t c = crc16_xmodem(b, n); b[n] = (uint8_t)(c >> 8); b[n + 1] = (uint8_t)c;
        zero += crc16_xmodem(b, n + 2) == 0;
    }
    CHECK(zero == 2000);

    /* A3: block 0 파싱 */
    char nm[64]; uint32_t sz = 0;
    const uint8_t b0a[] = "app.bin\0" "40960 15071360000 100644";
    CHECK(ym_parse_block0(b0a, sizeof b0a, nm, sizeof nm, &sz) == 0 && strcmp(nm, "app.bin") == 0 && sz == 40960u);
    const uint8_t b0b[] = "fw/v2.bin\0" "4294967295";
    CHECK(ym_parse_block0(b0b, sizeof b0b, nm, sizeof nm, &sz) == 0 && sz == 4294967295u);
    const uint8_t b0c[] = "x\0" "4294967296";         /* 32 비트 넘침 */
    CHECK(ym_parse_block0(b0c, sizeof b0c, nm, sizeof nm, &sz) == -1);
    uint8_t nul[128] = {0};
    CHECK(ym_parse_block0(nul, sizeof nul, nm, sizeof nm, &sz) == 1);         /* null block 0 */
    const uint8_t b0d[] = "app.bin\0";                 /* 크기 없음 → 부트로더는 거부 */
    CHECK(ym_parse_block0(b0d, sizeof b0d, nm, sizeof nm, &sz) == -1);
    const uint8_t b0e[] = "app.bin\0" "12a4";
    CHECK(ym_parse_block0(b0e, sizeof b0e, nm, sizeof nm, &sz) == -1);
    uint8_t nonul[128]; memset(nonul, 'a', sizeof nonul);
    CHECK(ym_parse_block0(nonul, sizeof nonul, nm, sizeof nm, &sz) == -1);   /* NUL 없음 */
    CHECK(ym_parse_block0(nonul, sizeof nonul, nm, 4, &sz) == -1);

    /* A2: 패킷 읽기 — 날 바이트 스트림을 직접 흘린다 */
    static uint8_t raw[1100]; uint32_t len = 0; uint8_t blk = 0, data[1024];
    raw[0] = SOH; raw[1] = 7; raw[2] = 0xF8;
    for (int i = 0; i < 128; i++) raw[3 + i] = (uint8_t)(i * 3);
    uint16_t c = crc16_xmodem(raw + 3, 128); raw[131] = (uint8_t)(c >> 8); raw[132] = (uint8_t)c;
    sim_reset_raw(raw, 133, 1024);
    CHECK(ym_read_packet(data, &len, &blk, 100) == PKT_OK && len == 128 && blk == 7 && data[127] == (uint8_t)(127 * 3));
    raw[2] = 0xF9; sim_reset_raw(raw, 133, 1024);
    CHECK(ym_read_packet(data, &len, &blk, 100) == PKT_BAD_BLK);
    raw[2] = 0xF8; raw[50] ^= 0x10; sim_reset_raw(raw, 133, 1024);
    CHECK(ym_read_packet(data, &len, &blk, 100) == PKT_BAD_CRC);
    raw[50] ^= 0x10; sim_reset_raw(raw, 132, 1024);           /* 마지막 바이트 없음 → 1 s 뒤 SHORT */
    CHECK(ym_read_packet(data, &len, &blk, 100) == PKT_SHORT && sim_now() == 132u + YM_CHAR_TO);
    sim_reset_raw(raw, 0, 1024);
    CHECK(ym_read_packet(data, &len, &blk, 777) == PKT_TIMEOUT && sim_now() == 777u);
    const uint8_t cc[] = {CAN, CAN}, cx[] = {CAN, 'x'}, eo[] = {EOT}, gb[] = {0x55};
    sim_reset_raw(cc, 2, 64); CHECK(ym_read_packet(data, &len, &blk, 100) == PKT_CAN);
    sim_reset_raw(cx, 2, 64); CHECK(ym_read_packet(data, &len, &blk, 100) == PKT_GARBAGE);  /* CAN 하나는 노이즈 */
    sim_reset_raw(eo, 1, 64); CHECK(ym_read_packet(data, &len, &blk, 100) == PKT_EOT);
    sim_reset_raw(gb, 1, 64); CHECK(ym_read_packet(data, &len, &blk, 100) == PKT_GARBAGE);
    raw[0] = STX; raw[1] = 0xFF; raw[2] = 0x00;                /* STX 1024 */
    for (int i = 0; i < 1024; i++) raw[3 + i] = (uint8_t)(i ^ 0x5A);
    c = crc16_xmodem(raw + 3, 1024); raw[1027] = (uint8_t)(c >> 8); raw[1028] = (uint8_t)c;
    sim_reset_raw(raw, 1029, 2048);
    CHECK(ym_read_packet(data, &len, &blk, 100) == PKT_OK && len == 1024 && blk == 0xFF);

    /* A4: 깨끗한 전송 — 응답 바이트열까지 정확히 */
    fault_t fx = fault_none();
    flash_init(&g_fl, 1);
    uint32_t n = make_image(g_img, 100, 0x01020003u, 11, 0);
    CHECK(xfer(g_img, n, &fx, ERASE_LAZY) == YM_OK);
    const uint8_t want[] = {'C', ACK, 'C', ACK, ACK, NAK, ACK, 'C', ACK};
    CHECK(S.resp_n == sizeof want && memcmp(S.resp, want, sizeof want) == 0);
    CHECK(slot_check() == IMG_OK && slot_equals(&g_fl, g_img, n) && S.st == SN_DONE);
    CHECK(strcmp(g_ym.name, "app.bin") == 0 && g_ym.size == n && g_ym.st.blocks == 2);

    /* 크기 경계 × 블록 모드: 내용, 패딩 자르기, 연산 수(erase = 닿은 섹터, program = 블록 + 헤더) */
    const uint32_t sizes[] = {65, 128, 129, 1023, 1024, 1025, 1024 + 127, 1024 + 128, 1024 + 129, 16384,
                              16385, 40000, 65536, SLOT_SIZE - 1u, SLOT_SIZE};
    uint32_t good = 0, cases = 0;
    for (int m = 0; m < 2; m++)
        for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
            fx = fault_none(); fx.only_128 = m == 1;
            flash_init(&g_fl, 2);
            n = make_image(g_img, sizes[k] - IMG_HDR_SIZE, 0x00010000u + k, 100 + k, k % 3 == 0 ? MK_TAIL_1A : 0);
            ym_rc_t rc = xfer(g_img, n, &fx, ERASE_LAZY);
            bool ok = rc == YM_OK && slot_check() == IMG_OK && slot_equals(&g_fl, g_img, n) &&
                      g_fl.n_erase == (n - 1u) / FL_SECTOR + 1u &&
                      g_fl.n_prog == n_data_blocks(n, fx.only_128) + 1u &&
                      (n == SLOT_SIZE || g_fl.mem[SLOT_ADDR - FL_BASE + n] == 0xFF) && boot_region_intact(&g_fl);
            if (!ok) printf("  size %u only128=%d rc=%s erase=%u prog=%u\n", (unsigned)n, m, ym_rc_name(rc),
                            (unsigned)g_fl.n_erase, (unsigned)g_fl.n_prog);
            good += ok; cases++;
        }
    CHECK(good == cases);
    CHECK(g_ym.st.blocks == SLOT_SIZE / 128u);                /* 768 블록: 블록 번호 0xFF→0x00 3 번 넘어감 */

    /* 파일 끝이 진짜 0x1A 여도 크기 덕분에 안 잘린다 */
    flash_init(&g_fl, 3); fx = fault_none();
    n = make_image(g_img, 1000, 1, 5, MK_TAIL_1A);
    CHECK(g_img[n - 1] == CPMEOF && xfer(g_img, n, &fx, ERASE_LAZY) == YM_OK && slot_equals(&g_fl, g_img, n));
}

static void test_part_b(void) {
    fault_t fx; ym_rc_t rc; uint32_t n;
    n = make_image(g_img, 20000, 0x02000000u, 77, 0);          /* 20064 B: 1K 블록 19 + 128 블록 5 */
    const uint32_t nb = n_data_blocks(n, false);

    /* 데이터 바이트 하나 비트 플립 → CRC 오류 1, NAK 1 → 재전송으로 성공 */
    flash_init(&g_fl, 1); fx = fault_none(); fx.flip_s2r_at = 133 + 1029 * 2 + 500;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_OK && g_ym.st.crc_errs == 1 && S.retx == 1 && slot_equals(&g_fl, g_img, n) && g_fl.n_prog == nb + 1u);
    /* 블록 번호 바이트 플립 → 보수 불일치 */
    flash_init(&g_fl, 1); fx = fault_none(); fx.flip_s2r_at = 133 + 1029 * 3 + 2;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_OK && g_ym.st.blk_errs == 1 && slot_equals(&g_fl, g_img, n));
    /* 시작 바이트 플립 → 쓰레기 → purge → NAK */
    flash_init(&g_fl, 1); fx = fault_none(); fx.flip_s2r_at = 133 + 1029 * 4;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_OK && g_ym.st.garbage + g_ym.st.eots >= 1 && g_ym.st.purged > 1000 && slot_equals(&g_fl, g_img, n));
    /* 바이트 유실 → 글자 타임아웃 (SHORT) → NAK */
    flash_init(&g_fl, 1); fx = fault_none(); fx.drop_s2r_at = 133 + 1029 * 5 + 77;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_OK && g_ym.st.shorts == 1 && slot_equals(&g_fl, g_img, n));
    /* ACK 유실 → 송신기 재전송 → 중복 블록: ACK 하고 다시 쓰지 않는다 */
    flash_init(&g_fl, 1); fx = fault_none(); fx.drop_r2s_at = 5;   /* 'C' ACK 'C' ACK(b1) ACK(b2) ACK(b3)=5 */
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_OK && g_ym.st.dups == 1 && g_fl.n_prog == nb + 1u && slot_equals(&g_fl, g_img, n));
    /* 송신기가 같은 블록을 두 번 보냄 */
    flash_init(&g_fl, 1); fx = fault_none(); fx.dup_blk = 7;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_OK && g_ym.st.dups == 1 && g_fl.n_prog == nb + 1u && slot_equals(&g_fl, g_img, n));
    /* block 0 의 ACK 유실 → block 0 중복 → ACK + 'C' 를 다시 */
    flash_init(&g_fl, 1); fx = fault_none(); fx.drop_r2s_at = 1;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_OK && g_ym.st.dups == 1 && slot_equals(&g_fl, g_img, n));
    /* 송신기가 늦게 켜짐 → 'C' 몇 번 */
    flash_init(&g_fl, 1); fx = fault_none(); fx.start_delay = 7u * TICKS_PER_S;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_OK && g_ym.st.c_sent == 4u + 2u);           /* t=0,3,6 s 는 무시됨, 9 s 응답 + b0 뒤 + EOT 뒤 */
    /* 첫 EOT 의 NAK 유실, 둘째 EOT 의 ACK 유실 → 둘 다 회복 */
    flash_init(&g_fl, 1); fx = fault_none(); fx.drop_r2s_at = (int32_t)(3u + nb);   /* EOT1 에 대한 NAK */
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_OK && slot_check() == IMG_OK);
    flash_init(&g_fl, 1); fx = fault_none(); fx.drop_r2s_at = (int32_t)(4u + nb);   /* EOT2 에 대한 ACK */
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_OK && slot_check() == IMG_OK && S.st == SN_DONE);
    /* 응답 바이트가 계속 사라지는 선: 마지막 null block 의 ACK 까지 */
    flash_init(&g_fl, 1); fx = fault_none(); fx.drop_r2s_at = (int32_t)(6u + nb);
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_OK && slot_check() == IMG_OK);              /* 수신기는 이미 끝; 송신기만 재시도 */

    /* 송신기가 블록 6 에서 죽음 → NAK 10 번 + 타임아웃 → CAN, 슬롯은 무효 */
    flash_init(&g_fl, 1); fx = fault_none(); fx.die_at_blk = 6;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_ERR_TIMEOUT && g_ym.st.timeouts == YM_MAX_ERRS + 1u && g_ym.st.naks == YM_MAX_ERRS);
    CHECK(S.resp_n >= 2 && S.resp[S.resp_n - 1] == CAN && S.resp[S.resp_n - 2] == CAN && slot_check() == IMG_NO_IMAGE);
    /* 중간 CAN CAN */
    flash_init(&g_fl, 1); fx = fault_none(); fx.cancel_at_blk = 9;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_ERR_CANCELLED && g_ym.st.blocks == 8 && g_sink.committed == 8u * 1024u && slot_check() == IMG_NO_IMAGE);
    /* 틀린 블록 번호 (송신기 버그: 4 를 건너뜀) → 동기 상실 → CAN */
    flash_init(&g_fl, 1); fx = fault_none(); fx.skip_seq_at_blk = 4;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_ERR_SEQ && g_ym.st.blocks == 3 && S.st == SN_ABORT && slot_check() == IMG_NO_IMAGE);
    /* 송신기가 블록 12 대신 EOT (잘린 전송) → 바이트 부족 → SHORT */
    flash_init(&g_fl, 1); fx = fault_none(); fx.eot_at_blk = 12;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_ERR_SHORT && g_ym.written == 11u * 1024u && slot_check() == IMG_NO_IMAGE);
    /* block 0 크기가 거짓 → 첫 블록의 헤더 payload_size 와 불일치 → 플래시 연산 0 */
    flash_init(&g_fl, 1); fx = fault_none(); fx.lie_size = 1000;
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_ERR_IMAGE && g_fl.op_index == 0 && slot_check() == IMG_NO_IMAGE);
    /* 슬롯보다 큰 파일 → block 0 에서 거절, 플래시 연산 0 */
    flash_init(&g_fl, 1); fx = fault_none();
    uint32_t big = make_image(g_img2, SLOT_SIZE - IMG_HDR_SIZE + 1u, 1, 3, 0);
    rc = xfer(g_img2, big, &fx, ERASE_LAZY);
    CHECK(rc == YM_ERR_TOO_BIG && g_fl.op_index == 0 && S.resp_n == 3 && S.resp[1] == CAN);
    /* 나쁜 이미지: magic → 첫 블록에서 fail-fast (erase 0), payload CRC → commit 거절, 크기 불일치 */
    flash_init(&g_fl, 1); fx = fault_none();
    uint32_t m = make_image(g_img2, 5000, 1, 3, MK_BAD_MAGIC);
    CHECK(xfer(g_img2, m, &fx, ERASE_LAZY) == YM_ERR_IMAGE && g_fl.op_index == 0 && g_ym.st.blocks == 0);
    flash_init(&g_fl, 1);
    m = make_image(g_img2, 5000, 1, 3, MK_SIZE_MISMATCH);
    CHECK(xfer(g_img2, m, &fx, ERASE_LAZY) == YM_ERR_IMAGE && g_fl.op_index == 0);
    flash_init(&g_fl, 1);
    m = make_image(g_img2, 5000, 1, 3, MK_BAD_CRC);
    rc = xfer(g_img2, m, &fx, ERASE_LAZY);
    CHECK(rc == YM_ERR_IMAGE && slot_hdr_blank(&g_fl) && slot_check() == IMG_NO_IMAGE && S.resp[S.resp_n - 1] == CAN);
    /* 0 으로 안 써지는 셀 → read-back verify 실패 → CAN, 무효 */
    flash_init(&g_fl, 1); fx = fault_none();
    g_fl.stuck1_addr = SLOT_ADDR + 3000u; g_fl.stuck1_mask = 0x80;
    g_img[3000] &= 0x7F;                                          /* 그 비트가 0 이어야 하는 데이터 */
    { img_hdr_t h; memset(&h, 0, sizeof h); h.magic = IMG_MAGIC; h.hdr_version = 1; h.hdr_size = 64;
      h.img_version = 0x02000000u; h.payload_size = n - 64u; h.load_addr = SLOT_ADDR + 64u;
      h.payload_crc32 = crc32_update(0, g_img + 64, n - 64u); hdr_encode(&h, g_img); }
    rc = xfer(g_img, n, &fx, ERASE_LAZY);
    CHECK(rc == YM_ERR_FLASH && g_sink.last_frc == FLASH_ERR_VERIFY && g_ym.st.blocks == 2 && slot_check() == IMG_NO_IMAGE);

    /* 고정 시드 랜덤 soak: 결과는 (성공 + 슬롯 == 파일) 또는 (깨끗한 실패 + 슬롯 무효) 둘 중 하나 */
    uint32_t rs = 0xC0FFEEu, ok = 0, clean_fail = 0, bad = 0, runs = 300, flips = 0;
    for (uint32_t t = 0; t < runs; t++) {
        fx = fault_none(); fx.seed = fl_rand(&rs);
        fx.p_flip_s2r = fl_rand(&rs) % 600u; fx.p_drop_s2r = fl_rand(&rs) % 200u;
        fx.p_drop_r2s = fl_rand(&rs) % 5000u; fx.p_flip_r2s = fl_rand(&rs) % 5000u;
        fx.only_128 = fl_rand(&rs) % 4u == 0;
        uint32_t pl = 1u + fl_rand(&rs) % 30000u;
        uint32_t fn = make_image(g_img2, pl, t, fl_rand(&rs), fl_rand(&rs) % 5u == 0 ? MK_TAIL_1A : 0);
        flash_init(&g_fl, t + 1u);
        rc = xfer(g_img2, fn, &fx, ERASE_LAZY);
        flips += g_ym.st.crc_errs + g_ym.st.blk_errs + g_ym.st.garbage + g_ym.st.shorts;
        img_rc_t ir = slot_check();
        if (rc == YM_OK && ir == IMG_OK && slot_equals(&g_fl, g_img2, fn)) ok++;
        else if (rc != YM_OK && ir != IMG_OK) clean_fail++;
        else { bad++; printf("  soak %u: rc=%s img=%s\n", (unsigned)t, ym_rc_name(rc), img_rc_name(ir)); }
        if (!boot_region_intact(&g_fl)) bad++;
    }
    if (g_verbose) printf("  soak: %u runs, ok %u, clean fail %u, bad %u, packet errors recovered %u\n",
                          (unsigned)runs, (unsigned)ok, (unsigned)clean_fail, (unsigned)bad, (unsigned)flips);
    CHECK(bad == 0 && ok + clean_fail == runs);
    CHECK(ok >= runs * 95u / 100u && flips > 500u);
}

static void test_part_c(void) {
    /* 싱크 단위 테스트: 섹터 경계를 넘는 쓰기, 비정렬, 같은 곳 두 번 */
    flash_init(&g_fl, 9);
    sink_t s; sink_init(&s, &g_fl, SLOT_ADDR, SLOT_SIZE, ERASE_LAZY);
    uint32_t n = make_image(g_img, 40000, 1, 21, 0);
    CHECK(sink_begin(&s, n) == SINK_OK && g_fl.op_index == 0);
    CHECK(sink_write(&s, 0, g_img, 1024) == SINK_OK && g_fl.n_erase == 1 && slot_hdr_blank(&g_fl));
    CHECK(sink_write(&s, 1024, g_img + 1024, 1024) == SINK_OK);
    CHECK(sink_write(&s, 16384u - 512u, g_img + 16384 - 512, 1024) == SINK_OK && g_fl.n_erase == 2);  /* 경계 넘음 */
    CHECK(sink_write(&s, 1027, g_img + 1027, 64) == SINK_ERR_SIZE);                                   /* 비정렬 */
    CHECK(sink_write(&s, 1024, g_img + 1024, 1024) == SINK_OK);           /* 같은 데이터 재기록: AND 로는 구분 불가 */
    uint8_t other[1024]; memcpy(other, g_img + 1024, 1024); other[10] ^= 0x01;
    CHECK(sink_write(&s, 1024, other, 1024) == SINK_ERR_FLASH);
    CHECK(s.last_frc == FLASH_ERR_NOT_ERASED);                            /* blank check 가 0→1 을 잡는다 */
    CHECK(sink_write(&s, n - 10u, g_img + n - 10u, 20) == SINK_ERR_SIZE); /* 파일 끝 넘음 */
    CHECK(sink_commit(&s) == SINK_ERR_IMAGE && slot_hdr_blank(&g_fl));   /* 다 안 받았으면 commit 거절 */
    sink_t s2; sink_init(&s2, &g_fl, SLOT_ADDR, SLOT_SIZE, ERASE_LAZY);
    CHECK(sink_begin(&s2, 64) == SINK_ERR_SIZE && sink_begin(&s2, SLOT_SIZE + 1u) == SINK_ERR_SIZE);
    CHECK(sink_write(&s2, 0, g_img, 0) == SINK_OK);

    /* 지우기 정책: lazy 는 닿은 섹터만, upfront-slot 은 6 개 다 */
    fault_t fx = fault_none();
    n = make_image(g_img, 20000, 0x01000000u, 31, 0);
    flash_init(&g_fl, 1);
    CHECK(xfer(g_img, n, &fx, ERASE_LAZY) == YM_OK && g_fl.n_erase == 2);
    flash_init(&g_fl, 1);
    CHECK(xfer(g_img, n, &fx, ERASE_UPFRONT_NEEDED) == YM_OK && g_fl.n_erase == 2);
    flash_init(&g_fl, 1);
    CHECK(xfer(g_img, n, &fx, ERASE_UPFRONT_SLOT) == YM_OK && g_fl.n_erase == 6 && slot_check() == IMG_OK);

    /* 전원 차단: 모든 플래시 연산 지점 × 시드 3. 처음엔 옛 이미지 v1 이 슬롯에 있다. */
    uint32_t old_n = make_image(g_img2, 30000, 0x01000000u, 41, 0);
    flash_init(&g_fl, 1); memcpy(g_fl.mem + (SLOT_ADDR - FL_BASE), g_img2, old_n);
    CHECK(slot_check() == IMG_OK);
    CHECK(xfer(g_img, n, &fx, ERASE_LAZY) == YM_OK);
    const uint32_t N = g_fl.op_index;                            /* 2 erase + 24 program + 1 헤더 = 27 */
    CHECK(N == 2u + n_data_blocks(n, false) + 1u);
    uint32_t scen = 0, refused = 0, blank_hdr = 0, hdr_torn_ok = 0, hdr_torn_bad = 0, recovered = 0, wrong = 0;
    for (uint32_t seed = 1; seed <= 3; seed++)
        for (uint32_t cut = 1; cut <= N; cut++) {
            flash_init(&g_fl, seed * 7919u + cut);
            memcpy(g_fl.mem + (SLOT_ADDR - FL_BASE), g_img2, old_n);
            g_fl.cut_at = cut;
            ym_rc_t rc = xfer(g_img, n, &fx, ERASE_LAZY);
            scen++;
            if (rc != YM_ERR_POWER) wrong++;
            flash_power_on(&g_fl);                               /* 재부팅 */
            img_rc_t ir = slot_check();
            if (cut < N) {
                if (ir != IMG_OK) refused++; else wrong++;
                if (cut >= 2 && slot_hdr_blank(&g_fl)) blank_hdr++;  /* 섹터 0 을 지운 뒤면 헤더 = 0xFF */
            } else {                                              /* 헤더 기록이 찢김: 받아들이면 내용이 정확해야 */
                if (ir == IMG_OK) { hdr_torn_ok++; if (!slot_equals(&g_fl, g_img, n)) wrong++; } else hdr_torn_bad++;
            }
            if (xfer(g_img, n, &fx, ERASE_LAZY) == YM_OK && slot_check() == IMG_OK && slot_equals(&g_fl, g_img, n))
                recovered++;                                      /* 다시 보내면 복구 */
            if (!boot_region_intact(&g_fl)) wrong++;
        }
    if (g_verbose) printf("  power-cut: N=%u ops, %u scenarios, refused %u/%u, header blank %u, header-op cut: "
                          "accepted(correct) %u / refused %u, recovered by resend %u\n", (unsigned)N, (unsigned)scen,
                          (unsigned)refused, (unsigned)(3u * (N - 1u)), (unsigned)blank_hdr, (unsigned)hdr_torn_ok,
                          (unsigned)hdr_torn_bad, (unsigned)recovered);
    CHECK(wrong == 0 && refused == 3u * (N - 1u) && blank_hdr == 3u * (N - 2u));
    CHECK(hdr_torn_ok + hdr_torn_bad == 3u && recovered == scen);

    /* 부트로더 시나리오: 빈 보드 → 복구 → 앱 / 전송 중 정전 → 다시 부트로더 → 재전송 → 앱 */
    g_log_n = 0; g_log[0] = '\0';
    flash_init(&g_fl, 5);
    n = make_image(g_img, 40000, 0x01020003u, 51, 0);
    fx = fault_none(); fx.p_flip_s2r = 150; fx.seed = 99; fx.drop_r2s_at = 9;
    sim_reset(g_img, n, "app_v1.2.3.bin", &fx);
    ym_rc_t yr = YM_ERR_NO_SENDER;
    CHECK(boot_main(&g_fl, &g_ym, &g_sink, &yr) == BOOT_APP && yr == YM_OK && strstr(g_log, "reset -> slot check: OK"));
    CHECK(boot_main(&g_fl, &g_ym, &g_sink, NULL) == BOOT_APP && strstr(g_log, "-> jump 0x08008040"));
    fx = fault_none();
    n = make_image(g_img, 50000, 0x01030000u, 52, 0);
    flash_init(&g_fl, 6); memcpy(g_fl.mem + (SLOT_ADDR - FL_BASE), g_img2, old_n);
    memset(g_fl.mem + (SLOT_ADDR - FL_BASE), 0x00, 4);           /* 옛 이미지 magic 깨짐 → 복구 모드 */
    g_fl.cut_at = 20;
    sim_reset(g_img, n, "app_v1.3.0.bin", &fx);
    CHECK(boot_main(&g_fl, &g_ym, &g_sink, &yr) == BOOT_STAY && yr == YM_ERR_POWER);
    flash_power_on(&g_fl);
    sim_reset(g_img, n, "app_v1.3.0.bin", &fx);
    CHECK(boot_main(&g_fl, &g_ym, &g_sink, &yr) == BOOT_APP && yr == YM_OK && slot_equals(&g_fl, g_img, n));
    CHECK(strstr(g_log, "slot@0x08008000 check: NO_IMAGE") != NULL && boot_region_intact(&g_fl));
    if (g_verbose) printf("%s", g_log);
}

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* YM_NO_MAIN */
