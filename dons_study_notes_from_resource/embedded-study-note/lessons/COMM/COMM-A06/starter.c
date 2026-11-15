/* COMM-A06 SPI NOR 플래시 드라이버: JEDEC ID, WEL, 페이지 프로그램
 *
 * mock SPI NOR (W25Q80 비슷한 1 MB, 페이지 256 B, 섹터 4 KB, 블록 64 KB — 값은 이 모델의 정의/대표값)를
 * 바이트 단위 SPI(CS + 1 바이트 교환)로 붙이고, 그 위에 드라이버와 작은 로그 저장소를 짠다.
 *   명령: 0x9F RDID, 0x06 WREN, 0x04 WRDI, 0x05 RDSR, 0x01 WRSR, 0x02 PP, 0x20 SE(4K), 0xD8 BE(64K), 0x03 READ
 *   규칙: 쓰기류 명령은 CS 상승 에지에서 실행, WEL=1 필요, 끝나면 WEL 자동 해제, WIP=1 동안 RDSR 외 전부 무시,
 *         PP 는 1→0 만 가능(AND), 페이지 끝을 넘으면 "같은 페이지 처음"으로 랩, READ 는 연속(끝에서 0 으로).
 *   1 tick = 1 µs, SPI 1 바이트 = 1 tick (SCLK 8 MHz 가정).
 * Part A: chip_on_byte / chip_on_cs_rise (칩 모델), nor_probe / nor_wait_ready / nor_write_enable / nor_write
 * Part B: nor_erase / nor_unprotect + 장애 주입 (busy 멈춤, WREN 무시, WP#, 전원 차단, MCU 리셋 중 erase)
 * Part C: log_mount / log_append — NOR 위의 append-only 로그 (레코드 헤더 + CRC-16, 부팅 스캔, 섹터 회전)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 이 레슨의 칩 정의 ---------------- */
#define NOR_MAX_SIZE (1u << 20)     /* mock 메모리 배열 크기 (1 MB) */
#define NOR_PAGE     256u
#define NOR_SECTOR   4096u
#define NOR_BLOCK    65536u

enum { CMD_WRSR = 0x01, CMD_PP = 0x02, CMD_READ = 0x03, CMD_WRDI = 0x04, CMD_RDSR = 0x05,
       CMD_WREN = 0x06, CMD_SE = 0x20, CMD_BE = 0xD8, CMD_RDID = 0x9F };
#define SR_WIP     0x01u   /* bit0 Write In Progress (= BUSY) */
#define SR_WEL     0x02u   /* bit1 Write Enable Latch */
#define SR_BP_MASK 0x1Cu   /* bit4..2 BP2..BP0 블록 보호 */
#define SR_SRP     0x80u   /* bit7 SRP: 1 이고 WP# = low 면 WRSR 무시 (하드웨어 보호) */
#define SR_NV_MASK 0xFCu   /* WRSR 가 쓰는 비휘발 비트 (bit5 TB, bit6 SEC 는 저장만, 이 모델은 해석 안 함) */

/* 칩 내부 동작 시간 (tick = µs) — W25Q 계열 typical 대표값, 부품별 데이터시트 확인 */
#define T_PP 700u
#define T_SE 45000u
#define T_BE 150000u
#define T_W  10000u
/* 드라이버 타임아웃 (데이터시트 max 대표값) 과 RDSR 폴링 간격 */
#define TO_PP 3000u
#define TO_SE 400000u
#define TO_BE 2000000u
#define TO_W  15000u
#define POLL_PP 50u
#define POLL_SE 1000u
#define POLL_BE 5000u
#define POLL_W  500u

enum { NOR_OK = 0, NOR_E_ARG = -1, NOR_E_ALIGN = -2, NOR_E_TIMEOUT = -3, NOR_E_NODEV = -4,
       NOR_E_ID = -5, NOR_E_WEL = -6, NOR_E_PROTECTED = -7, NOR_E_BUSY = -8 };
enum { OP_NONE = 0, OP_PP, OP_SE, OP_BE, OP_WRSR, OP_COUNT };

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ================= mock SPI NOR ================= */
typedef struct NorChip {
    uint32_t size;                /* 2^n 바이트, ≤ NOR_MAX_SIZE */
    uint8_t  id[3];               /* JEDEC: 제조사, 메모리 타입, 용량 코드 */
    uint8_t  sr;                  /* 상태 레지스터 (비휘발 BP/SRP + 휘발 WIP/WEL) */
    bool     wp_low;              /* WP# 핀 = low */
    /* 한 CS 프레임의 상태 (CS↓ 에서 리셋) */
    bool     cs_low, ignore;
    uint32_t nbytes;              /* 이 프레임에서 받은 바이트 수 (명령 포함) */
    uint8_t  cmd, wrsr_val;
    uint32_t addr;                /* 명령 뒤 24 비트 주소 (MSB first), 3 바이트째에 칩 크기로 마스크 */
    uint8_t  pbuf[NOR_PAGE];      /* PP 래치. 0xFF = 안 바꿈. 열(column) = (addr + i) & 0xFF */
    /* 진행 중인 내부 동작 (WIP = 1) */
    uint8_t  op, op_val;
    uint32_t op_addr, op_total, op_left, op_start;
    uint8_t  op_data[NOR_PAGE];
    /* 장애 주입 */
    bool     present, powered, stuck_busy, drop_wren, cut_armed;
    uint8_t  miso_idle;           /* 칩이 MISO 를 안 몰 때 보이는 값 (풀업 0xFF, 고장 시 0x00) */
    uint32_t cut_at, rng;
    /* 통계 */
    uint32_t n_exec[OP_COUNT];    /* 실제로 시작된 내부 동작 수 */
    uint32_t n_ign_busy, n_ign_wel, n_ign_prot;
    uint8_t  cut_op;              /* 마지막 전원 차단 때 진행 중이던 동작 (OP_NONE = 쉬는 중) */
    uint8_t  mem[NOR_MAX_SIZE];
} NorChip;

static uint32_t g_now;            /* 시뮬레이션 시각 (tick = µs) */

/* 이 모델의 BP 표 (W25Q 류, TB = 0: 맨 위부터 보호). 보호 영역 = [from, size). BP = 0 → from = size */
uint32_t bp_protected_from(uint8_t sr, uint32_t size) {
    unsigned bp = (sr & SR_BP_MASK) >> 2;
    if (bp == 0) return size;
    uint32_t span = NOR_BLOCK << (bp - 1);            /* BP 1..7 → 64K, 128K, 256K, ... */
    return span >= size ? 0 : size - span;
}

void chip_init(NorChip *c, uint32_t size, uint8_t m, uint8_t type, uint8_t cap) {
    memset(c, 0, offsetof(NorChip, mem));
    memset(c->mem, 0xFF, sizeof c->mem);              /* 공장 출하 = 지워진 상태 */
    c->size = size; c->id[0] = m; c->id[1] = type; c->id[2] = cap;
    c->present = c->powered = true; c->miso_idle = 0xFF; c->rng = 0x2545F491u;
}

void chip_start_op(NorChip *c, uint8_t op, uint32_t addr, uint32_t t) {
    c->op = op; c->op_addr = addr; c->op_total = c->op_left = t; c->op_start = g_now;
    c->sr |= SR_WIP; c->n_exec[op]++;
}

static void chip_finish_op(NorChip *c) {              /* 내부 동작 완료 → 메모리 반영, WIP·WEL 해제 */
    if (c->op == OP_PP) for (uint32_t i = 0; i < NOR_PAGE; i++) c->mem[c->op_addr + i] &= c->op_data[i];
    if (c->op == OP_SE) memset(c->mem + c->op_addr, 0xFF, NOR_SECTOR);
    if (c->op == OP_BE) memset(c->mem + c->op_addr, 0xFF, NOR_BLOCK);
    if (c->op == OP_WRSR) c->sr = (uint8_t)((c->sr & ~SR_NV_MASK) | (c->op_val & SR_NV_MASK));
    c->op = OP_NONE;
    c->sr &= (uint8_t)~(SR_WIP | SR_WEL);
}

/* 전원 차단: 진행 중이던 동작은 경과 비율 f 만큼만 반영 (모델: 바뀔 비트마다 확률 f 로 바뀜) */
static void chip_cut_now(NorChip *c) {
    c->cut_op = c->op;
    if (c->op != OP_NONE) {
        uint32_t f = (uint32_t)((uint64_t)(g_now - c->op_start) * 1024u / c->op_total);   /* 0..1023 */
        if (c->op == OP_PP) {
            for (uint32_t i = 0; i < NOR_PAGE; i++) {
                uint8_t clr = (uint8_t)(c->mem[c->op_addr + i] & ~c->op_data[i]);
                for (unsigned b = 0; b < 8; b++)
                    if (((clr >> b) & 1u) && xs32(&c->rng) % 1024u < f) c->mem[c->op_addr + i] &= (uint8_t)~(1u << b);
            }
        } else if (c->op == OP_SE || c->op == OP_BE) {
            uint32_t n = c->op == OP_SE ? NOR_SECTOR : NOR_BLOCK;
            for (uint32_t i = 0; i < n; i++) {
                uint8_t set = (uint8_t)~c->mem[c->op_addr + i];
                for (unsigned b = 0; set && b < 8; b++)
                    if (((set >> b) & 1u) && xs32(&c->rng) % 1024u < f) c->mem[c->op_addr + i] |= (uint8_t)(1u << b);
            }
        } else if (c->op == OP_WRSR && f >= 512u) {
            c->sr = (uint8_t)((c->sr & ~SR_NV_MASK) | (c->op_val & SR_NV_MASK));
        }
    }
    c->op = OP_NONE; c->powered = false; c->cut_armed = false;
}

/* 시간 진행: 내부 동작 카운트다운, 예약된 전원 차단 */
void chip_advance(NorChip *c, uint32_t ticks) {
    while (ticks > 0) {
        if (c->cut_armed && (int32_t)(c->cut_at - g_now) <= 0) { chip_cut_now(c); continue; }
        uint32_t step = ticks;
        if (c->cut_armed && c->cut_at - g_now < step) step = c->cut_at - g_now;
        bool running = c->op != OP_NONE && !c->stuck_busy;
        if (running && c->op_left < step) step = c->op_left;
        g_now += step; ticks -= step;
        if (running) { c->op_left -= step; if (c->op_left == 0) chip_finish_op(c); }
    }
    if (c->cut_armed && (int32_t)(c->cut_at - g_now) <= 0) chip_cut_now(c);
}

void chip_power_on(NorChip *c) {                       /* 전원 재인가: 휘발 상태(WIP, WEL, 프레임)만 리셋 */
    c->powered = true; c->stuck_busy = false; c->op = OP_NONE; c->cs_low = false;
    c->sr &= SR_NV_MASK;
}

uint8_t chip_on_byte(NorChip *c, uint32_t idx, uint8_t mosi);
void    chip_on_cs_rise(NorChip *c);

void chip_cs(NorChip *c, int level) {
    if (level == 0 && !c->cs_low) {                    /* CS↓: 새 프레임 */
        c->cs_low = true; c->nbytes = 0; c->ignore = false; c->addr = 0; c->cmd = 0;
        memset(c->pbuf, 0xFF, sizeof c->pbuf);
    } else if (level != 0 && c->cs_low) {              /* CS↑: 쓰기류 명령은 여기서 실행 */
        c->cs_low = false;
        if (c->present && c->powered) chip_on_cs_rise(c);
    }
}

uint8_t chip_xfer(NorChip *c, uint8_t mosi) {         /* SPI 1 바이트 교환 = 1 tick */
    chip_advance(c, 1);
    if (!c->cs_low || !c->present || !c->powered) return c->miso_idle;
    uint32_t idx = c->nbytes++;
    return chip_on_byte(c, idx, mosi);
}

/* TODO(Part A) chip_on_byte: 프레임의 idx 번째 바이트(0 = 명령)를 받는다. 반환 = 그 바이트 동안의 MISO.
 *   busy(WIP=1) 중 RDSR 이 아닌 명령은 프레임 전체를 무시(ignore) 하고 MISO = miso_idle.
 *   RDSR: 매 바이트 최신 SR.  RDID: id[0..2] 다음은 miso_idle.  WRSR: 1 번째 데이터 바이트를 wrsr_val 에.
 *   주소 명령(READ/PP/SE/BE): 1..3 번째 = 주소 MSB first (3 번째에서 size-1 로 마스크).
 *   READ: mem[(addr + k) & (size-1)] — 페이지 랩 없음.  PP: pbuf[(addr + k) & 0xFF] = mosi — 페이지 안 랩. */
uint8_t chip_on_byte(NorChip *c, uint32_t idx, uint8_t mosi) {
    (void)idx; (void)mosi;
    return c->miso_idle;
}

/* TODO(Part A) chip_on_cs_rise: CS↑ 에서 명령 실행.
 *   무시된 프레임 / 빈 프레임 → 아무것도 안 함.  WREN → WEL=1 (drop_wren 장애면 무시), WRDI → WEL=0.
 *   WRSR(≥2 B) / PP(≥5 B) / SE·BE(정확히 4 B): 길이 틀림 → 무시, WEL=0 → 무시(n_ign_wel),
 *   WRSR 은 SRP=1 && WP# low → 무시(n_ign_prot), PP/SE/BE 는 대상 영역이 BP 보호 영역과 겹치면 무시(n_ign_prot).
 *   통과하면 chip_start_op (PP 는 pbuf 를 op_data 로 복사, base = 페이지/섹터/블록 정렬 주소). */
void chip_on_cs_rise(NorChip *c) {
    (void)c;
}

/* ================= 드라이버 ================= */
typedef struct NorDev {
    NorChip    *chip;
    uint32_t    size;             /* probe 가 채움. 0 = probe 전 → 모든 접근 E_ARG */
    uint8_t     id[3];
    const char *vendor;
    uint32_t    polls;            /* RDSR 프레임 수 */
} NorDev;

void    spi_select(NorDev *d)             { chip_cs(d->chip, 0); }
void    spi_deselect(NorDev *d)           { chip_cs(d->chip, 1); }
uint8_t spi_byte(NorDev *d, uint8_t b)    { return chip_xfer(d->chip, b); }
void    sim_delay(NorDev *d, uint32_t t)  { chip_advance(d->chip, t); }
void    nor_init(NorDev *d, NorChip *c)   { memset(d, 0, sizeof *d); d->chip = c; }

void nor_cmd(NorDev *d, uint8_t cmd) { spi_select(d); spi_byte(d, cmd); spi_deselect(d); }
uint8_t nor_rdsr(NorDev *d) {
    spi_select(d); spi_byte(d, CMD_RDSR); uint8_t v = spi_byte(d, 0xFF); spi_deselect(d);
    d->polls++;
    return v;
}
void nor_send_addr(NorDev *d, uint32_t a) {           /* 24 비트 주소 MSB first */
    spi_byte(d, (uint8_t)(a >> 16)); spi_byte(d, (uint8_t)(a >> 8)); spi_byte(d, (uint8_t)a);
}
void nor_read_id(NorDev *d, uint8_t id[3]) {
    spi_select(d); spi_byte(d, CMD_RDID);
    for (int i = 0; i < 3; i++) id[i] = spi_byte(d, 0xFF);
    spi_deselect(d);
}
const char *nor_vendor_name(uint8_t m) {               /* JEP106 bank 1 코드 (기억에 의존 — 확인 권장) */
    switch (m) {
    case 0xEF: return "Winbond";    case 0xC2: return "Macronix";      case 0x20: return "Micron/ST";
    case 0xC8: return "GigaDevice"; case 0x9D: return "ISSI";          case 0x1F: return "Adesto/Atmel";
    case 0xBF: return "SST/Microchip";
    default:   return NULL;
    }
}
bool id_blank(const uint8_t id[3]) {
    return (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF) || (id[0] == 0 && id[1] == 0 && id[2] == 0);
}
int nor_read(NorDev *d, uint32_t addr, uint8_t *buf, size_t len) {   /* READ 0x03: 한 프레임 연속 읽기 */
    if (len && !buf) return NOR_E_ARG;
    if (addr > d->size || len > d->size - addr) return NOR_E_ARG;
    if (len == 0) return NOR_OK;
    spi_select(d); spi_byte(d, CMD_READ); nor_send_addr(d, addr);
    for (size_t i = 0; i < len; i++) buf[i] = spi_byte(d, 0xFF);
    spi_deselect(d);
    return NOR_OK;
}

int nor_wait_ready(NorDev *d, uint32_t timeout, uint32_t poll);

/* TODO(Part A) nor_probe: JEDEC ID 를 읽어 size/vendor 를 채운다.
 *   ID 가 FF FF FF / 00 00 00 이면 "칩 없음" 또는 "MCU 리셋 때 진행 중이던 erase 가 아직 busy" 일 수 있다
 *   → nor_wait_ready(TO_BE, POLL_BE) 후 한 번 더 읽고, 그래도 그러면 E_NODEV.
 *   제조사 모름 → E_ID.  용량 코드 0x10..0x18 (64 KB..16 MB, 3 바이트 주소 한계) 밖 → E_ID.  size = 1 << 코드. */
int nor_probe(NorDev *d) {
    d->size = 0; d->vendor = NULL;
    return NOR_E_NODEV;
}

/* TODO(Part A) nor_wait_ready: RDSR 를 읽어 WIP=0 이면 OK. 아니면 시작부터 timeout tick 이 지났으면 E_TIMEOUT,
 *   아니면 sim_delay(poll) 하고 다시. 경과 시간은 (uint32_t)(g_now - t0) 로 (랩어라운드 안전). */
int nor_wait_ready(NorDev *d, uint32_t timeout, uint32_t poll) {
    (void)d; (void)timeout; (void)poll;
    return NOR_E_TIMEOUT;
}

/* TODO(Part A) nor_write_enable: WREN 을 보내고 RDSR 로 확인. (WEL=1 이고 WIP=0) 이 아니면 E_WEL. */
int nor_write_enable(NorDev *d) {
    (void)d;
    return NOR_E_WEL;
}

/* TODO(Part A) nor_write: [addr, addr+len) 에 프로그램 (1→0 만 — 지우기는 호출자 책임).
 *   인자/범위 → E_ARG.  len 0 → OK.  시작 전 RDSR: WIP → E_BUSY, 범위가 BP 보호 영역과 겹치면 E_PROTECTED.
 *   페이지 경계에서 자른다: 조각마다 write_enable → PP(주소 3 B + 데이터) → CS↑ → wait_ready(TO_PP, POLL_PP).
 *   어느 단계든 실패하면 그 코드를 바로 돌려준다 (다음 페이지를 보내지 않는다). */
int nor_write(NorDev *d, uint32_t addr, const uint8_t *buf, size_t len) {
    (void)d; (void)addr; (void)buf; (void)len;
    return NOR_E_ARG;
}

/* TODO(Part B) nor_erase: [addr, addr+len) 을 0xFF 로. addr·len 이 4 KB 배수가 아니면 E_ALIGN, 범위 → E_ARG,
 *   len 0 → OK, WIP → E_BUSY, 보호 영역과 겹치면 E_PROTECTED.
 *   64 KB 정렬이고 64 KB 이상 남았으면 BE(0xD8, TO_BE/POLL_BE), 아니면 SE(0x20, TO_SE/POLL_SE). 매번 write_enable. */
int nor_erase(NorDev *d, uint32_t addr, uint32_t len) {
    (void)d; (void)addr; (void)len;
    return NOR_E_ARG;
}

/* TODO(Part B) nor_unprotect: BP 비트를 0 으로. 이미 0 → OK.  WIP → E_BUSY.
 *   write_enable → WRSR(0x01, sr & ~BP & NV_MASK — SRP 등은 유지) → wait_ready(TO_W, POLL_W) → RDSR 재확인.
 *   BP 가 그대로면 (WP# + SRP 하드웨어 보호) WRDI 로 남은 WEL 을 지우고 E_PROTECTED. */
int nor_unprotect(NorDev *d) {
    (void)d;
    return NOR_E_PROTECTED;
}

/* ================= Part C: NOR 위의 append-only 로그 ================= */
/* 섹터 헤더 (8 B): 'L' 'G' seq(LE32) crc16(앞 6 B, LE)
 * 레코드 (len+4 B): 0xA5 len(1..128) payload[len] crc16(0xA5, len, payload — LE).  섹터를 넘지 않는다.
 * 섹터 안의 첫 0xFF 매직 = 로그 끝.  섹터 회전: 다음 섹터 erase → 헤더(seq+1) → 그 섹터에 이어 쓰기. */
#define LOG_HDR_SIZE    8u
#define LOG_REC_MAGIC   0xA5u
#define LOG_MAX_PAYLOAD 128u
#define LOG_MAX_NSEC    16u

typedef struct LogStore {
    NorDev  *d;
    uint32_t base, nsec;              /* 영역 = base(섹터 정렬)부터 nsec 개 섹터 */
    uint32_t cur, seq;                /* 지금 쓰는 섹터 번호와 그 세대 번호 */
    uint32_t wp;                      /* 다음 레코드를 쓸 절대 주소 */
    bool     need_rotate;             /* 현재 섹터의 남은 칸이 깨끗하지 않음 → 다음 append 는 새 섹터에서 */
    uint32_t n_records, n_corrupt;    /* mount 때 현재 섹터 스캔 결과 */
    uint32_t n_formatted, n_rotations, n_appended;
} LogStore;
typedef void (*log_cb_t)(void *ctx, const uint8_t *p, uint32_t len, uint32_t seq);

uint16_t crc16_ccitt(const uint8_t *p, size_t n) {   /* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF (ALGO-M01) */
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
    }
    return c;
}

bool log_read_header(LogStore *ls, uint32_t sec, uint32_t *seq) {
    uint8_t h[LOG_HDR_SIZE];
    if (nor_read(ls->d, ls->base + sec * NOR_SECTOR, h, sizeof h) != NOR_OK) return false;
    if (h[0] != 'L' || h[1] != 'G') return false;
    if (crc16_ccitt(h, 6) != (uint16_t)(h[6] | h[7] << 8)) return false;
    *seq = (uint32_t)h[2] | (uint32_t)h[3] << 8 | (uint32_t)h[4] << 16 | (uint32_t)h[5] << 24;
    return true;
}

int log_start_sector(LogStore *ls, uint32_t sec, uint32_t seq) {   /* erase → 헤더 기록 */
    uint32_t a = ls->base + sec * NOR_SECTOR;
    int rc = nor_erase(ls->d, a, NOR_SECTOR);
    if (rc != NOR_OK) return rc;
    uint8_t h[LOG_HDR_SIZE] = { 'L', 'G', (uint8_t)seq, (uint8_t)(seq >> 8), (uint8_t)(seq >> 16), (uint8_t)(seq >> 24), 0, 0 };
    uint16_t c = crc16_ccitt(h, 6);
    h[6] = (uint8_t)c; h[7] = (uint8_t)(c >> 8);
    return nor_write(ls->d, a, h, sizeof h);
}

/* 섹터 하나를 앞에서부터 훑는다. *end = 첫 0xFF 매직의 오프셋. 쓰레기 헤더를 만나면 *end = NOR_SECTOR (그 뒤 못 씀).
 * CRC 가 틀린 레코드(찢어진 레코드)는 len 만큼 건너뛰고 n_bad++. cb 는 CRC 가 맞는 레코드만 받는다. */
int log_scan_sector(LogStore *ls, uint32_t sec, uint32_t seq, log_cb_t cb, void *ctx,
                    uint32_t *end, uint32_t *n_ok, uint32_t *n_bad) {
    uint8_t rec[LOG_MAX_PAYLOAD + 4];
    uint32_t a0 = ls->base + sec * NOR_SECTOR, off = LOG_HDR_SIZE;
    *n_ok = *n_bad = 0;
    while (off + 5 <= NOR_SECTOR) {
        int rc = nor_read(ls->d, a0 + off, rec, 2);
        if (rc != NOR_OK) return rc;
        if (rec[0] == 0xFF) break;                                       /* 지워진 칸 = 끝 */
        uint32_t len = rec[1];
        if (rec[0] != LOG_REC_MAGIC || len == 0 || len > LOG_MAX_PAYLOAD || off + len + 4 > NOR_SECTOR) {
            (*n_bad)++; off = NOR_SECTOR; break;                          /* 길이를 믿을 수 없음 → 섹터 끝 */
        }
        rc = nor_read(ls->d, a0 + off + 2, rec + 2, len + 2);
        if (rc != NOR_OK) return rc;
        if (crc16_ccitt(rec, len + 2) == (uint16_t)(rec[len + 2] | rec[len + 3] << 8)) {
            (*n_ok)++;
            if (cb) cb(ctx, rec + 2, len, seq);
        } else {
            (*n_bad)++;
        }
        off += len + 4;
    }
    *end = off;
    return NOR_OK;
}

/* 살아 있는 섹터들을 seq 오름차순(오래된 것부터)으로 훑어 CRC 가 맞는 레코드를 cb 로 넘긴다 */
int log_iterate(LogStore *ls, log_cb_t cb, void *ctx) {
    uint32_t secs[LOG_MAX_NSEC], seqs[LOG_MAX_NSEC], n = 0;
    for (uint32_t i = 0; i < ls->nsec; i++) {
        uint32_t s;
        if (!log_read_header(ls, i, &s) || s > ls->seq || ls->seq - s >= ls->nsec) continue;
        uint32_t j = n++;
        while (j > 0 && seqs[j - 1] > s) { seqs[j] = seqs[j - 1]; secs[j] = secs[j - 1]; j--; }
        seqs[j] = s; secs[j] = i;
    }
    for (uint32_t k = 0; k < n; k++) {
        uint32_t end, ok, bad;
        int rc = log_scan_sector(ls, secs[k], seqs[k], cb, ctx, &end, &ok, &bad);
        if (rc != NOR_OK) return rc;
    }
    return NOR_OK;
}

/* TODO(Part C) log_mount: ls 를 0 으로 채우고 d/base/nsec 기록. 인자 검사(nsec 2..16, base 섹터 정렬,
 *   영역이 칩 안) → E_ARG.  모든 섹터 헤더를 읽어 seq 가 가장 큰 섹터 = cur.  유효한 헤더가 없으면 포맷:
 *   log_start_sector(0, seq 1), wp = 섹터 0 + 8, n_formatted = 1.
 *   있으면 log_scan_sector 로 끝을 찾아 wp, n_records, n_corrupt 를 채우고,
 *   wp 부터 섹터 끝까지 전부 0xFF 인지 확인(blank check) — 아니면 need_rotate = true. */
int log_mount(LogStore *ls, NorDev *d, uint32_t base, uint32_t nsec) {
    memset(ls, 0, sizeof *ls);
    (void)d; (void)base; (void)nsec;
    return NOR_E_ARG;
}

/* TODO(Part C) log_append: 레코드 하나 추가. len 1..128, data != NULL, mount 됨(nsec != 0) 아니면 E_ARG.
 *   need_rotate 이거나 현재 섹터에 len+4 B 가 안 들어가면 회전: next = (cur+1) % nsec, log_start_sector(next, seq+1)
 *   — 실패하면 need_rotate = true 로 두고 그 코드 반환. 성공하면 cur/seq/wp 갱신, need_rotate=false, n_rotations++.
 *   레코드 [0xA5][len][payload][crc16 LE] 를 nor_write. 실패 → need_rotate = true (반쯤 써졌을 수 있음), 코드 반환.
 *   성공 → wp += len+4, n_appended++. */
int log_append(LogStore *ls, const uint8_t *data, uint32_t len) {
    (void)ls; (void)data; (void)len;
    return NOR_E_ARG;
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static NorChip g_chip;
static NorDev  g_dev;
static uint8_t g_ref[NOR_MAX_SIZE];
static uint8_t g_buf[8192], g_buf2[8192];

static int fresh(void) {                                  /* 새 칩(W25Q80 류: EF 40 14) + probe */
    chip_init(&g_chip, NOR_MAX_SIZE, 0xEF, 0x40, 0x14);
    nor_init(&g_dev, &g_chip);
    return nor_probe(&g_dev);
}
static void raw_frame(const uint8_t *tx, size_t n, uint8_t *rx) {   /* 드라이버를 거치지 않는 프레임 하나 */
    spi_select(&g_dev);
    for (size_t i = 0; i < n; i++) { uint8_t v = spi_byte(&g_dev, tx[i]); if (rx) rx[i] = v; }
    spi_deselect(&g_dev);
}
static void raw_cmd_addr(uint8_t cmd, uint32_t a, const uint8_t *data, size_t n) {
    spi_select(&g_dev); spi_byte(&g_dev, cmd); nor_send_addr(&g_dev, a);
    for (size_t i = 0; i < n; i++) spi_byte(&g_dev, data[i]);
    spi_deselect(&g_dev);
}
/* 버그 드라이버: 페이지 경계를 무시하고 PP 한 번에 전부 보낸다 */
static int naive_write_nosplit(NorDev *d, uint32_t addr, const uint8_t *buf, size_t len) {
    int rc = nor_write_enable(d);
    if (rc != NOR_OK) return rc;
    spi_select(d); spi_byte(d, CMD_PP); nor_send_addr(d, addr);
    for (size_t i = 0; i < len; i++) spi_byte(d, buf[i]);
    spi_deselect(d);
    return nor_wait_ready(d, TO_PP, POLL_PP);
}

static void test_part_a(void) {
    printf("-- Part A: 칩 모델, probe, WEL, 페이지 프로그램\n");
    CHECK(fresh() == NOR_OK);
    CHECK(g_dev.size == (1u << 20) && g_dev.vendor && strcmp(g_dev.vendor, "Winbond") == 0);
    CHECK(g_dev.id[0] == 0xEF && g_dev.id[1] == 0x40 && g_dev.id[2] == 0x14);

    /* WREN / WRDI / RDSR, CS 상승 에지에서 실행 */
    CHECK(nor_rdsr(&g_dev) == 0x00);
    spi_select(&g_dev); spi_byte(&g_dev, CMD_WREN);
    CHECK((g_chip.sr & SR_WEL) == 0);                       /* CS 가 아직 low → 실행 전 */
    spi_deselect(&g_dev);
    CHECK((g_chip.sr & SR_WEL) != 0);
    CHECK(nor_rdsr(&g_dev) == SR_WEL);
    nor_cmd(&g_dev, CMD_WRDI);
    CHECK(nor_rdsr(&g_dev) == 0x00);
    CHECK(nor_write_enable(&g_dev) == NOR_OK && nor_rdsr(&g_dev) == SR_WEL);
    nor_cmd(&g_dev, CMD_WRDI);

    /* WREN 없이 PP → 무시 */
    uint8_t z[4] = { 0, 0, 0, 0 };
    uint32_t ign = g_chip.n_ign_wel;
    raw_cmd_addr(CMD_PP, 0x3000, z, 4);
    CHECK(g_chip.n_ign_wel == ign + 1 && g_chip.mem[0x3000] == 0xFF && nor_rdsr(&g_dev) == 0);

    /* PP 진행 중: WIP, RDSR 만 응답, 나머지 무시, 끝나면 WEL 자동 해제 */
    uint8_t d16[16];
    for (int i = 0; i < 16; i++) d16[i] = (uint8_t)(0x10 + i);
    CHECK(nor_write_enable(&g_dev) == NOR_OK);
    raw_cmd_addr(CMD_PP, 0x4000, d16, 16);
    CHECK(nor_rdsr(&g_dev) == (SR_WIP | SR_WEL));
    uint8_t tx[8] = { CMD_RDID, 0, 0, 0 }, rx[8];
    raw_frame(tx, 4, rx);
    CHECK(rx[1] == 0xFF && rx[2] == 0xFF && rx[3] == 0xFF);   /* busy 중 RDID 는 무시 → MISO 풀업 */
    uint8_t rd[5] = { CMD_READ, 0x00, 0x40, 0x00, 0 }, rr[5];
    raw_frame(rd, 5, rr);
    CHECK(rr[4] == 0xFF);                                       /* busy 중 READ 도 무시 */
    nor_cmd(&g_dev, CMD_WREN);
    raw_cmd_addr(CMD_SE, 0x4000, NULL, 0);                      /* busy 중 erase → 무시 */
    CHECK(g_chip.n_ign_busy >= 3 && g_chip.n_exec[OP_SE] == 0);
    CHECK(nor_wait_ready(&g_dev, TO_PP, POLL_PP) == NOR_OK);
    CHECK(nor_rdsr(&g_dev) == 0x00);                            /* WEL 자동 해제 */
    CHECK(nor_read(&g_dev, 0x4000, g_buf, 17) == NOR_OK && memcmp(g_buf, d16, 16) == 0 && g_buf[16] == 0xFF);

    /* AND 의미: 지우지 않고 다시 쓰면 1→0 만 */
    uint8_t f0 = 0xF0, x3c = 0x3C;
    CHECK(nor_write(&g_dev, 0x5000, &f0, 1) == NOR_OK && nor_write(&g_dev, 0x5000, &x3c, 1) == NOR_OK);
    CHECK(nor_read(&g_dev, 0x5000, g_buf, 1) == NOR_OK && g_buf[0] == 0x30);

    /* 칩 모델의 페이지 랩: 32 B 를 0x1F0 에 PP 한 번 → 뒤 16 B 는 0x100 으로 */
    uint8_t d300[300];
    for (int i = 0; i < 300; i++) d300[i] = (uint8_t)(i * 7 + 3);
    CHECK(nor_write_enable(&g_dev) == NOR_OK);
    raw_cmd_addr(CMD_PP, 0x1F0, d300, 32);
    CHECK(nor_wait_ready(&g_dev, TO_PP, POLL_PP) == NOR_OK);
    CHECK(memcmp(g_chip.mem + 0x1F0, d300, 16) == 0 && memcmp(g_chip.mem + 0x100, d300 + 16, 16) == 0);
    CHECK(g_chip.mem[0x200] == 0xFF && g_chip.mem[0x110] == 0xFF);

    /* 300 B 를 PP 한 번 (256 초과) → 같은 열은 나중 바이트가 이긴다 */
    chip_init(&g_chip, NOR_MAX_SIZE, 0xEF, 0x40, 0x14);
    CHECK(nor_write_enable(&g_dev) == NOR_OK);
    raw_cmd_addr(CMD_PP, 0x1F0, d300, 300);
    CHECK(nor_wait_ready(&g_dev, TO_PP, POLL_PP) == NOR_OK && g_chip.n_exec[OP_PP] == 1);
    CHECK(g_chip.mem[0x100] == d300[272] && g_chip.mem[0x11B] == d300[299] && g_chip.mem[0x11C] == d300[44]);
    CHECK(g_chip.mem[0x1F0] == d300[256] && g_chip.mem[0x1FF] == d300[271] && g_chip.mem[0x200] == 0xFF);

    /* 페이지 랩 버그: 단순 드라이버 vs 페이지 분할 드라이버 (300 B @ 0x1F0) */
    chip_init(&g_chip, NOR_MAX_SIZE, 0xEF, 0x40, 0x14);
    CHECK(naive_write_nosplit(&g_dev, 0x1F0, d300, 300) == NOR_OK);       /* 에러 없이 "성공" 한다 */
    CHECK(nor_read(&g_dev, 0x1F0, g_buf, 300) == NOR_OK);
    int mism = 0;
    for (int i = 0; i < 300; i++) mism += g_buf[i] != d300[i];
    CHECK(mism > 0);                                                      /* 읽어 보면 틀렸다 */
    CHECK(g_buf[16] == 0xFF && g_buf[299] == 0xFF);                      /* 0x200.. 은 한 번도 안 써짐 */
    printf("   naive PP 300 B @0x1F0: %d/300 bytes wrong, PP commands = %u\n", mism, (unsigned)g_chip.n_exec[OP_PP]);
    chip_init(&g_chip, NOR_MAX_SIZE, 0xEF, 0x40, 0x14);
    CHECK(nor_write(&g_dev, 0x1F0, d300, 300) == NOR_OK);
    CHECK(nor_read(&g_dev, 0x1F0, g_buf, 300) == NOR_OK && memcmp(g_buf, d300, 300) == 0);
    CHECK(g_chip.n_exec[OP_PP] == 3);                                    /* 16 + 256 + 28 */
    CHECK(g_chip.mem[0x100] == 0xFF && g_chip.mem[0x1EF] == 0xFF && g_chip.mem[0x31C] == 0xFF);

    /* 경계: 정확히 한 페이지, 1 바이트, 마지막 바이트, 범위 밖 */
    chip_init(&g_chip, NOR_MAX_SIZE, 0xEF, 0x40, 0x14);
    CHECK(nor_write(&g_dev, 0x800, d300, 256) == NOR_OK && g_chip.n_exec[OP_PP] == 1);
    CHECK(nor_write(&g_dev, 0xFFFFF, d300, 1) == NOR_OK && g_chip.mem[0xFFFFF] == d300[0]);
    CHECK(nor_write(&g_dev, 0xFFFFF, d300, 2) == NOR_E_ARG);
    CHECK(nor_write(&g_dev, 0x100000, d300, 1) == NOR_E_ARG && nor_write(&g_dev, 0, NULL, 4) == NOR_E_ARG);
    CHECK(nor_write(&g_dev, 0x900, d300, 0) == NOR_OK && g_chip.n_exec[OP_PP] == 2);

    /* READ 는 연속: 페이지 경계를 넘고, 칩 끝에서 0 으로 */
    CHECK(nor_read(&g_dev, 0x8F0, g_buf, 32) == NOR_OK && memcmp(g_buf, d300 + 240, 16) == 0 && g_buf[16] == 0xFF);
    uint8_t one = 0x5A;
    CHECK(nor_write(&g_dev, 0, &one, 1) == NOR_OK);
    uint8_t rw[7] = { CMD_READ, 0x0F, 0xFF, 0xFF, 0, 0, 0 }, rwx[7];
    raw_frame(rw, 7, rwx);
    CHECK(rwx[4] == d300[0] && rwx[5] == 0x5A && rwx[6] == 0xFF);       /* 0xFFFFF → 0x00000 → 0x00001 */
    CHECK(nor_read(&g_dev, 0xFFFFF, g_buf, 2) == NOR_E_ARG);             /* 드라이버는 랩을 허용하지 않는다 */
}

static void test_part_b(void) {
    printf("-- Part B: erase, 보호, 장애 주입, 랜덤 대조\n");
    CHECK(fresh() == NOR_OK);
    /* erase 인자 */
    CHECK(nor_erase(&g_dev, 0x1001, 4096) == NOR_E_ALIGN && nor_erase(&g_dev, 0x1000, 100) == NOR_E_ALIGN);
    CHECK(nor_erase(&g_dev, 0x100000, 4096) == NOR_E_ARG && nor_erase(&g_dev, 0xFF000, 8192) == NOR_E_ARG);
    CHECK(nor_erase(&g_dev, 0x2000, 0) == NOR_OK && g_chip.n_exec[OP_SE] == 0);

    /* erase-before-write: 0x55 위에 0xAA → 0x00, erase 뒤에야 0xAA */
    memset(g_buf, 0x55, 64); memset(g_buf2, 0xAA, 64);
    CHECK(nor_write(&g_dev, 0x7000, g_buf, 64) == NOR_OK && nor_write(&g_dev, 0x7000, g_buf2, 64) == NOR_OK);
    CHECK(nor_read(&g_dev, 0x7000, g_buf, 64) == NOR_OK && g_buf[0] == 0x00 && g_buf[63] == 0x00);
    memset(g_chip.mem + 0x6FF0, 0x11, 16); memset(g_chip.mem + 0x8000, 0x22, 16);   /* 이웃 섹터 표식 */
    CHECK(nor_erase(&g_dev, 0x7000, 4096) == NOR_OK && g_chip.n_exec[OP_SE] == 1);
    CHECK(nor_write(&g_dev, 0x7000, g_buf2, 64) == NOR_OK);
    CHECK(nor_read(&g_dev, 0x7000, g_buf, 64) == NOR_OK && memcmp(g_buf, g_buf2, 64) == 0);
    CHECK(g_chip.mem[0x6FFF] == 0x11 && g_chip.mem[0x8000] == 0x22 && g_chip.mem[0x7FFF] == 0xFF);

    /* SE / BE 선택: 0xF000 + 0x12000 → SE @F000, BE @10000, SE @20000 */
    uint32_t se0 = g_chip.n_exec[OP_SE], be0 = g_chip.n_exec[OP_BE];
    memset(g_chip.mem + 0xE000, 0, 0x14000);
    CHECK(nor_erase(&g_dev, 0xF000, 0x12000) == NOR_OK);
    CHECK(g_chip.n_exec[OP_SE] - se0 == 2 && g_chip.n_exec[OP_BE] - be0 == 1);
    CHECK(g_chip.mem[0xEFFF] == 0 && g_chip.mem[0xF000] == 0xFF && g_chip.mem[0x20FFF] == 0xFF && g_chip.mem[0x21000] == 0);
    se0 = g_chip.n_exec[OP_SE]; be0 = g_chip.n_exec[OP_BE];
    CHECK(nor_erase(&g_dev, 0x40000, 0x21000) == NOR_OK);
    CHECK(g_chip.n_exec[OP_SE] - se0 == 1 && g_chip.n_exec[OP_BE] - be0 == 2);

    /* 잘못된 길이의 SE 프레임 (주소 뒤 1 바이트 더) → 실행 안 됨 */
    g_chip.mem[0x9000] = 0;
    CHECK(nor_write_enable(&g_dev) == NOR_OK);
    uint8_t extra = 0;
    raw_cmd_addr(CMD_SE, 0x9000, &extra, 1);
    CHECK(g_chip.mem[0x9000] == 0 && (nor_rdsr(&g_dev) & SR_WIP) == 0);
    nor_cmd(&g_dev, CMD_WRDI);

    /* 블록 보호: BP=1 → 맨 위 64 KB (0xF0000..) */
    CHECK(nor_write_enable(&g_dev) == NOR_OK);
    uint8_t wrsr[2] = { CMD_WRSR, 0x04 };
    raw_frame(wrsr, 2, NULL);
    CHECK(nor_wait_ready(&g_dev, TO_W, POLL_W) == NOR_OK && nor_rdsr(&g_dev) == 0x04);
    uint32_t pp0 = g_chip.n_exec[OP_PP];
    CHECK(nor_write(&g_dev, 0xF0000, g_buf2, 16) == NOR_E_PROTECTED && g_chip.n_exec[OP_PP] == pp0);
    CHECK(nor_write(&g_dev, 0xEFFF0, g_buf2, 32) == NOR_E_PROTECTED);      /* 걸치기만 해도 거절 */
    CHECK(nor_erase(&g_dev, 0xF0000, 4096) == NOR_E_PROTECTED);
    CHECK(nor_write(&g_dev, 0xEFFF0, g_buf2, 16) == NOR_OK);
    uint32_t ip = g_chip.n_ign_prot;
    CHECK(nor_write_enable(&g_dev) == NOR_OK);
    raw_cmd_addr(CMD_PP, 0xF0000, g_buf2, 16);                              /* 드라이버 우회 → 칩이 무시 */
    CHECK(g_chip.n_ign_prot == ip + 1 && g_chip.mem[0xF0000] == 0xFF && nor_rdsr(&g_dev) == (0x04 | SR_WEL));
    nor_cmd(&g_dev, CMD_WRDI);
    CHECK(nor_unprotect(&g_dev) == NOR_OK && nor_rdsr(&g_dev) == 0x00);
    CHECK(nor_write(&g_dev, 0xF0000, g_buf2, 16) == NOR_OK && g_chip.mem[0xF0000] == 0xAA);
    CHECK(nor_unprotect(&g_dev) == NOR_OK);                                  /* 이미 풀림 → 그대로 OK */

    /* 출하 때 전부 보호 (BP=7) */
    CHECK(fresh() == NOR_OK);
    g_chip.sr = 0x1C;
    CHECK(nor_write(&g_dev, 0, g_buf2, 1) == NOR_E_PROTECTED && nor_erase(&g_dev, 0, 4096) == NOR_E_PROTECTED);
    CHECK(nor_unprotect(&g_dev) == NOR_OK && nor_write(&g_dev, 0, g_buf2, 1) == NOR_OK);

    /* WP# 하드웨어 보호: SRP=1 + WP# low → WRSR 무시, 드라이버는 E_PROTECTED + WEL 정리 */
    CHECK(fresh() == NOR_OK);
    g_chip.sr = SR_SRP | 0x1C; g_chip.wp_low = true;
    CHECK(nor_unprotect(&g_dev) == NOR_E_PROTECTED);
    CHECK(nor_rdsr(&g_dev) == (SR_SRP | 0x1C) && g_chip.n_ign_prot == 1);     /* WEL 이 남아 있지 않다 */
    g_chip.wp_low = false;
    CHECK(nor_unprotect(&g_dev) == NOR_OK && nor_rdsr(&g_dev) == SR_SRP);     /* SRP 는 유지 */

    /* busy 멈춤: WIP 가 영원히 1 → E_TIMEOUT, 다음 페이지를 보내지 않는다 */
    CHECK(fresh() == NOR_OK);
    g_chip.stuck_busy = true;
    uint32_t t0 = g_now;
    CHECK(nor_write(&g_dev, 0x10000, g_buf2, 600) == NOR_E_TIMEOUT);
    uint32_t el = g_now - t0;
    CHECK(g_chip.n_exec[OP_PP] == 1 && el >= TO_PP && el < TO_PP + 270 + POLL_PP + 8);    /* 프레임 265 tick + 폴링 1 번 오차 */
    printf("   stuck busy: nor_write -> E_TIMEOUT after %u ticks (TO_PP %u), %u RDSR polls\n", (unsigned)el, TO_PP, (unsigned)g_dev.polls);
    CHECK(nor_erase(&g_dev, 0x10000, 4096) == NOR_E_BUSY && nor_write(&g_dev, 0, g_buf2, 1) == NOR_E_BUSY);
    chip_power_on(&g_chip);                                                    /* 전원 재인가로 복구 */
    CHECK(nor_rdsr(&g_dev) == 0 && nor_write(&g_dev, 0x20000, g_buf2, 8) == NOR_OK);

    /* WREN 이 안 먹는 칩 → E_WEL, PP 는 안 나감 */
    CHECK(fresh() == NOR_OK);
    g_chip.drop_wren = true;
    CHECK(nor_write(&g_dev, 0, g_buf2, 8) == NOR_E_WEL && g_chip.n_exec[OP_PP] == 0);
    CHECK(nor_erase(&g_dev, 0, 4096) == NOR_E_WEL && nor_unprotect(&g_dev) == NOR_OK);

    /* probe 장애 */
    chip_init(&g_chip, NOR_MAX_SIZE, 0xEF, 0x40, 0x14); g_chip.present = false;       /* 칩 없음, MISO 풀업 */
    nor_init(&g_dev, &g_chip);
    CHECK(nor_probe(&g_dev) == NOR_E_NODEV && g_dev.size == 0);
    g_chip.miso_idle = 0x00;                                                            /* MISO 가 GND 에 붙음 */
    CHECK(nor_probe(&g_dev) == NOR_E_NODEV);
    chip_init(&g_chip, NOR_MAX_SIZE, 0x42, 0x40, 0x14);
    CHECK(nor_probe(&g_dev) == NOR_E_ID);
    chip_init(&g_chip, NOR_MAX_SIZE, 0xEF, 0x40, 0x19);                                /* 32 MB → 4 바이트 주소 */
    CHECK(nor_probe(&g_dev) == NOR_E_ID);
    chip_init(&g_chip, NOR_MAX_SIZE, 0xC2, 0x20, 0x14);
    CHECK(nor_probe(&g_dev) == NOR_OK && strcmp(g_dev.vendor, "Macronix") == 0);

    /* MCU 리셋 순간 64 KB erase 진행 중 → 칩은 리셋 안 됨 → probe 가 기다렸다가 성공 */
    CHECK(fresh() == NOR_OK);
    memset(g_chip.mem + 0x30000, 0, NOR_BLOCK);
    nor_cmd(&g_dev, CMD_WREN);
    raw_cmd_addr(CMD_BE, 0x30000, NULL, 0);
    sim_delay(&g_dev, 20000);
    nor_init(&g_dev, &g_chip);                                                          /* MCU 만 리셋 */
    uint8_t idb[3];
    nor_read_id(&g_dev, idb);
    CHECK(idb[0] == 0xFF && idb[1] == 0xFF);                                            /* 그냥 읽으면 "칩 없음" */
    CHECK(nor_probe(&g_dev) == NOR_OK && g_dev.size == NOR_MAX_SIZE);
    CHECK(g_chip.mem[0x30000] == 0xFF && g_chip.mem[0x3FFFF] == 0xFF && nor_rdsr(&g_dev) == 0);

    /* 전원 차단: PP 중간 → 일부 비트만 */
    CHECK(fresh() == NOR_OK);
    memset(g_buf2, 0xAA, 256); memset(g_buf, 0x00, 256);
    CHECK(nor_write(&g_dev, 0x50000, g_buf2, 256) == NOR_OK);                          /* old = 0xAA */
    g_chip.cut_armed = true; g_chip.cut_at = g_now + 3 + 1 + 260 + T_PP / 2;           /* WREN+RDSR, PP 프레임, 반쯤 */
    CHECK(nor_write(&g_dev, 0x50000, g_buf, 256) == NOR_E_TIMEOUT && !g_chip.powered);
    chip_power_on(&g_chip);
    CHECK(nor_rdsr(&g_dev) == 0x00);
    CHECK(nor_read(&g_dev, 0x50000, g_buf, 256) == NOR_OK);
    int sub = 1, partial = 0, zeros = 0;
    for (int i = 0; i < 256; i++) {
        if (g_buf[i] & ~0xAA) sub = 0;                      /* 1 이 새로 생기면 안 된다 */
        partial += g_buf[i] != 0x00 && g_buf[i] != 0xAA;
        zeros += g_buf[i] == 0x00;
    }
    CHECK(sub && partial > 0 && zeros < 256);
    printf("   power cut at 1/2 t_PP: %d/256 bytes partially programmed, %d done, %d untouched\n",
           partial, zeros, 256 - partial - zeros);
    /* 전원 차단: SE 중간 → 일부만 지워짐 */
    memset(g_chip.mem + 0x60000, 0x00, NOR_SECTOR);
    g_chip.cut_armed = true; g_chip.cut_at = g_now + 3 + 4 + 4 + T_SE / 4;
    CHECK(nor_erase(&g_dev, 0x60000, NOR_SECTOR) == NOR_E_TIMEOUT);
    chip_power_on(&g_chip);
    CHECK(nor_read(&g_dev, 0x60000, g_buf, NOR_SECTOR) == NOR_OK);
    int ff = 0, nz = 0;
    for (uint32_t i = 0; i < NOR_SECTOR; i++) { ff += g_buf[i] == 0xFF; nz += g_buf[i] != 0; }
    CHECK(ff < (int)NOR_SECTOR && nz > 0);

    /* 랜덤 연산 vs 기준 RAM 모델 (NOR 의미: 프로그램 = AND, erase = 0xFF) */
    CHECK(fresh() == NOR_OK);
    memset(g_ref, 0xFF, sizeof g_ref);
    uint32_t s = 0xC0FFEEu, bad_rc = 0, bad_cmp = 0, nw = 0, ne = 0, nr = 0;
    for (int op = 0; op < 4000; op++) {
        uint32_t r = xs32(&s) % 100u;
        if (r < 45) {
            uint32_t a = xs32(&s) % NOR_MAX_SIZE, n = 1 + xs32(&s) % 700u;
            if (n > NOR_MAX_SIZE - a) n = NOR_MAX_SIZE - a;
            for (uint32_t i = 0; i < n; i++) { g_buf[i] = (uint8_t)xs32(&s); if (xs32(&s) % 4 == 0) g_buf[i] = 0xFF; }
            for (uint32_t i = 0; i < n; i++) g_ref[a + i] &= g_buf[i];
            bad_rc += nor_write(&g_dev, a, g_buf, n) != NOR_OK; nw++;
        } else if (r < 60) {
            uint32_t a, n;
            if (xs32(&s) % 5 == 0) { a = (xs32(&s) % 16u) * NOR_BLOCK; n = NOR_BLOCK; }
            else { a = (xs32(&s) % 256u) * NOR_SECTOR; n = (1 + xs32(&s) % 3u) * NOR_SECTOR; if (n > NOR_MAX_SIZE - a) n = NOR_MAX_SIZE - a; }
            memset(g_ref + a, 0xFF, n);
            bad_rc += nor_erase(&g_dev, a, n) != NOR_OK; ne++;
        } else {
            uint32_t a = xs32(&s) % NOR_MAX_SIZE, n = 1 + xs32(&s) % 2000u;
            if (n > NOR_MAX_SIZE - a) n = NOR_MAX_SIZE - a;
            bad_rc += nor_read(&g_dev, a, g_buf, n) != NOR_OK; nr++;
            bad_cmp += memcmp(g_buf, g_ref + a, n) != 0;
        }
    }
    int full = 1;
    for (uint32_t a = 0; a < NOR_MAX_SIZE; a += 8192) {
        if (nor_read(&g_dev, a, g_buf, 8192) != NOR_OK || memcmp(g_buf, g_ref + a, 8192) != 0) full = 0;
    }
    CHECK(bad_rc == 0 && bad_cmp == 0 && full && nw > 1500 && ne > 500 && nr > 1500);
    printf("   random ops: %u writes, %u erases, %u reads, rc errors %u, read mismatches %u, full compare %s\n",
           (unsigned)nw, (unsigned)ne, (unsigned)nr, (unsigned)bad_rc, (unsigned)bad_cmp, full ? "OK" : "BAD");
}

/* ---- Part C 도우미: 시도 번호로 만든 결정적 레코드 ---- */
#define C_MAX_ATTEMPTS 200000u
static uint32_t c_hash(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
static uint32_t make_payload(uint32_t idx, uint8_t *p) {
    uint32_t len = 4 + c_hash(idx) % 60u;                        /* 4..63 B */
    p[0] = (uint8_t)idx; p[1] = (uint8_t)(idx >> 8); p[2] = (uint8_t)(idx >> 16); p[3] = (uint8_t)(idx >> 24);
    for (uint32_t j = 4; j < len; j++) p[j] = (uint8_t)c_hash(idx * 131u + j);
    return len;
}
typedef struct { uint32_t n, bad, order_bad, last; uint32_t idx[2048]; uint32_t seq[2048]; } Collect;
static void collect_cb(void *ctx, const uint8_t *p, uint32_t len, uint32_t seq) {
    Collect *c = ctx;
    uint8_t want[LOG_MAX_PAYLOAD];
    uint32_t idx = len >= 4 ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
    if (len < 4 || idx >= C_MAX_ATTEMPTS || make_payload(idx, want) != len || memcmp(want, p, len) != 0) { c->bad++; return; }
    if (c->n > 0 && idx <= c->last) c->order_bad++;
    c->last = idx;
    if (c->n < 2048) { c->idx[c->n] = idx; c->seq[c->n] = seq; }
    c->n++;
}
static Collect g_col;
typedef struct { uint32_t n, first, last, bad; } Seq60;      /* 60 B 레코드: 번호가 1 씩 늘어야 */
static void seq60_cb(void *ctx, const uint8_t *p, uint32_t len, uint32_t seq) {
    Seq60 *q = ctx;
    (void)seq;
    uint32_t i = (uint32_t)p[0] | (uint32_t)p[1] << 8;
    if (len != 60 || (q->n > 0 && i != q->last + 1)) q->bad++;
    if (q->n == 0) q->first = i;
    q->last = i; q->n++;
}
static uint8_t  g_acked[C_MAX_ATTEMPTS];
static uint32_t g_acked_seq[C_MAX_ATTEMPTS];

static void test_part_c(void) {
    printf("-- Part C: NOR 로그 저장소\n");
    CHECK(crc16_ccitt((const uint8_t *)"123456789", 9) == 0x29B1);
    LogStore ls;
    const uint32_t BASE = 0x80000, NSEC = 4;

    /* 부팅 로그: 출하 보호 해제 → 마운트(포맷) → 기록 */
    CHECK(fresh() == NOR_OK);
    g_chip.sr = 0x1C;
    printf("   [nor] JEDEC %02X %02X %02X -> %s, %u KiB (page %u B, sector %u KiB)\n", g_dev.id[0], g_dev.id[1],
           g_dev.id[2], g_dev.vendor ? g_dev.vendor : "?", (unsigned)(g_dev.size / 1024), NOR_PAGE, NOR_SECTOR / 1024);
    CHECK(log_mount(&ls, &g_dev, BASE, NSEC) == NOR_E_PROTECTED);
    uint8_t sr_before = nor_rdsr(&g_dev);
    CHECK(nor_unprotect(&g_dev) == NOR_OK);
    printf("   [nor] SR=0x%02X (BP=%u, protected from 0x%06X) -> unprotect -> SR=0x%02X\n", sr_before,
           (unsigned)((sr_before & SR_BP_MASK) >> 2), (unsigned)bp_protected_from(sr_before, g_dev.size), nor_rdsr(&g_dev));
    CHECK(log_mount(&ls, &g_dev, BASE, NSEC) == NOR_OK);
    CHECK(ls.n_formatted == 1 && ls.seq == 1 && ls.cur == 0 && ls.wp == BASE + 8 && !ls.need_rotate);
    printf("   [log] mount @0x%06X x%u: formatted=%u seq=%u wp=0x%06X\n", (unsigned)BASE, (unsigned)NSEC,
           (unsigned)ls.n_formatted, (unsigned)ls.seq, (unsigned)ls.wp);
    uint8_t p[LOG_MAX_PAYLOAD + 1];
    uint32_t n0 = make_payload(0, p);
    CHECK(log_append(&ls, p, n0) == NOR_OK);
    uint32_t n1 = make_payload(1, p);
    CHECK(log_append(&ls, p, n1) == NOR_OK);
    memset(p, 0x42, sizeof p);
    CHECK(log_append(&ls, p, 0) == NOR_E_ARG && log_append(&ls, p, LOG_MAX_PAYLOAD + 1) == NOR_E_ARG);
    CHECK(log_append(&ls, NULL, 5) == NOR_E_ARG);
    uint32_t n2 = make_payload(2, p);
    CHECK(log_append(&ls, p, n2) == NOR_OK);
    uint32_t wp3 = ls.wp;
    CHECK(wp3 == BASE + 8 + n0 + n1 + n2 + 12);
    memset(&g_col, 0, sizeof g_col);
    CHECK(log_iterate(&ls, collect_cb, &g_col) == NOR_OK);
    CHECK(g_col.n == 3 && g_col.bad == 0 && g_col.idx[0] == 0 && g_col.idx[2] == 2);

    /* 재부팅(전원 차단 없음) → 같은 끝을 찾는다 */
    CHECK(log_mount(&ls, &g_dev, BASE, NSEC) == NOR_OK);
    CHECK(ls.n_formatted == 0 && ls.n_records == 3 && ls.n_corrupt == 0 && ls.wp == wp3 && !ls.need_rotate);
    printf("   [log] remount: seq=%u records=%u corrupt=%u wp=0x%06X\n", (unsigned)ls.seq, (unsigned)ls.n_records,
           (unsigned)ls.n_corrupt, (unsigned)ls.wp);
    CHECK(log_mount(&ls, &g_dev, BASE + 1, NSEC) == NOR_E_ARG && log_mount(&ls, &g_dev, BASE, 1) == NOR_E_ARG);
    CHECK(log_mount(&ls, &g_dev, 0xFE000, 4) == NOR_E_ARG);

    /* 섹터 회전: 60 B 페이로드(레코드 64 B) 400 개 → 섹터당 63 개, 7 세대, 살아 있는 4 섹터 = 189..399 */
    CHECK(fresh() == NOR_OK);
    CHECK(log_mount(&ls, &g_dev, BASE, NSEC) == NOR_OK);
    int all_ok = 1;
    for (uint32_t i = 0; i < 400; i++) {
        memset(p, 0, 60);
        p[0] = (uint8_t)i; p[1] = (uint8_t)(i >> 8);
        all_ok &= log_append(&ls, p, 60) == NOR_OK;
        all_ok &= (ls.wp - 1) / NOR_SECTOR == (ls.wp - 64) / NOR_SECTOR;     /* 레코드가 섹터를 넘지 않음 */
    }
    CHECK(all_ok && ls.n_rotations == 6 && ls.seq == 7);
    Seq60 q = { 0, 0, 0, 0 };
    CHECK(log_iterate(&ls, seq60_cb, &q) == NOR_OK);
    CHECK(q.n == 211 && q.first == 189 && q.last == 399 && q.bad == 0);
    printf("   [log] 400 appends: rotations=%u seq=%u alive records=%u (%u..%u)\n", (unsigned)ls.n_rotations,
           (unsigned)ls.seq, (unsigned)q.n, (unsigned)q.first, (unsigned)q.last);

    /* 찢어진 레코드: 11 번째 레코드 프로그램 중 전원 차단 */
    CHECK(fresh() == NOR_OK);
    CHECK(log_mount(&ls, &g_dev, BASE, NSEC) == NOR_OK);
    for (uint32_t i = 0; i < 10; i++) { uint32_t n = make_payload(i, p); CHECK(log_append(&ls, p, n) == NOR_OK); }
    uint32_t n10 = make_payload(10, p);
    g_chip.cut_armed = true; g_chip.cut_at = g_now + 300;
    CHECK(log_append(&ls, p, n10) != NOR_OK && !g_chip.powered);
    chip_power_on(&g_chip);
    CHECK(log_mount(&ls, &g_dev, BASE, NSEC) == NOR_OK);
    printf("   [log] torn record: remount records=%u corrupt=%u need_rotate=%d wp=0x%06X\n",
           (unsigned)ls.n_records, (unsigned)ls.n_corrupt, ls.need_rotate, (unsigned)ls.wp);
    CHECK(ls.n_records == 10 && (ls.n_corrupt == 1 || ls.need_rotate));
    memset(&g_col, 0, sizeof g_col);
    CHECK(log_iterate(&ls, collect_cb, &g_col) == NOR_OK && g_col.n == 10 && g_col.bad == 0 && g_col.last == 9);
    uint32_t n11 = make_payload(11, p);
    CHECK(log_append(&ls, p, n11) == NOR_OK);
    memset(&g_col, 0, sizeof g_col);
    CHECK(log_iterate(&ls, collect_cb, &g_col) == NOR_OK && g_col.n == 11 && g_col.bad == 0 && g_col.last == 11);

    /* 쓰기 실패 뒤 재마운트 없이 계속 (플래시 전원 레일만 복구): 실패한 자리 위에 다시 쓰면 안 된다 */
    CHECK(fresh() == NOR_OK);
    CHECK(log_mount(&ls, &g_dev, BASE, NSEC) == NOR_OK);
    for (uint32_t i = 0; i < 3; i++) { uint32_t n = make_payload(i, p); CHECK(log_append(&ls, p, n) == NOR_OK); }
    uint32_t n3 = make_payload(3, p);
    g_chip.cut_armed = true; g_chip.cut_at = g_now + 300;
    CHECK(log_append(&ls, p, n3) != NOR_OK && ls.need_rotate);
    chip_power_on(&g_chip);
    uint32_t n4 = make_payload(4, p);
    CHECK(log_append(&ls, p, n4) == NOR_OK && ls.cur == 1 && ls.seq == 2 && !ls.need_rotate);
    memset(&g_col, 0, sizeof g_col);
    CHECK(log_iterate(&ls, collect_cb, &g_col) == NOR_OK && g_col.n == 4 && g_col.bad == 0 && g_col.last == 4);

    /* 회전 중 erase 에서 전원 차단 → 직전 섹터들은 그대로, 다음 append 가 다시 회전 */
    CHECK(fresh() == NOR_OK);
    CHECK(log_mount(&ls, &g_dev, BASE, NSEC) == NOR_OK);
    uint32_t idx = 0, n = 0;
    all_ok = 1;
    for (;;) {                                                 /* 4 번째 섹터(seq 4)를 다음 레코드가 안 들어갈 때까지 */
        n = make_payload(idx, p);
        if (ls.seq == 4 && ls.wp + n + 4 > BASE + (ls.cur + 1) * NOR_SECTOR) break;
        all_ok &= log_append(&ls, p, n) == NOR_OK;
        if (++idx > 2000) break;
    }
    CHECK(all_ok && ls.seq == 4 && ls.cur == 3);
    uint32_t seq_before = ls.seq;
    g_chip.cut_armed = true; g_chip.cut_at = g_now + 200 + T_SE / 3;              /* 다음 섹터(가장 오래된) erase 중 */
    CHECK(log_append(&ls, p, n) != NOR_OK && !g_chip.powered && g_chip.n_exec[OP_SE] >= 4);
    chip_power_on(&g_chip);
    CHECK(log_mount(&ls, &g_dev, BASE, NSEC) == NOR_OK && ls.seq == seq_before);
    memset(&g_col, 0, sizeof g_col);
    CHECK(log_iterate(&ls, collect_cb, &g_col) == NOR_OK && g_col.bad == 0 && g_col.order_bad == 0);
    CHECK(g_col.n > 0 && g_col.last == idx - 1);
    CHECK(log_append(&ls, p, n) == NOR_OK && ls.seq == seq_before + 1);
    memset(&g_col, 0, sizeof g_col);
    CHECK(log_iterate(&ls, collect_cb, &g_col) == NOR_OK && g_col.last == idx && g_col.bad == 0);

    /* 랜덤 전원 차단 캠페인: 매번 다시 마운트해 불변식 검사 */
    CHECK(fresh() == NOR_OK);
    CHECK(log_mount(&ls, &g_dev, BASE, NSEC) == NOR_OK);
    memset(g_acked, 0, sizeof g_acked);
    uint32_t s = 0xBEEF01u, att = 0, cuts = 0, inv_bad = 0, lost = 0, torn_seen = 0, rot_after = 0, appended_after = 0;
    for (int cyc = 0; cyc < 300 && att + 400 < C_MAX_ATTEMPTS; cyc++) {
        uint32_t win = xs32(&s) % 2 ? 3000u : 200000u;
        g_chip.cut_armed = true; g_chip.cut_at = g_now + 1 + xs32(&s) % win;
        for (int k = 0; k < 400 && g_chip.powered; k++) {
            uint32_t len = make_payload(att, p);
            int rc = log_append(&ls, p, len);
            if (rc == NOR_OK) { g_acked[att] = 1; g_acked_seq[att] = ls.seq; }
            att++;
            if (rc != NOR_OK) break;
        }
        if (g_chip.powered) { g_chip.cut_armed = false; inv_bad++; continue; }     /* 차단 전에 끝나면 안 됨 */
        cuts++;
        chip_power_on(&g_chip);
        if (log_mount(&ls, &g_dev, BASE, NSEC) != NOR_OK) { inv_bad++; break; }
        torn_seen += ls.n_corrupt > 0; rot_after += ls.need_rotate;
        memset(&g_col, 0, sizeof g_col);
        if (log_iterate(&ls, collect_cb, &g_col) != NOR_OK) { inv_bad++; break; }
        if (g_col.bad || g_col.order_bad || g_col.n > 2048) inv_bad++;
        /* 보장 구간: 최근 (nsec-1) 세대 섹터에 ack 된 레코드는 전부 있어야 한다 */
        uint32_t k = 0;
        for (uint32_t i = 0; i < att; i++) {
            if (!g_acked[i] || g_acked_seq[i] + (NSEC - 2) < ls.seq) continue;
            while (k < g_col.n && g_col.idx[k] < i) k++;
            if (k >= g_col.n || g_col.idx[k] != i) lost++;
        }
        uint32_t len = make_payload(att, p);                                   /* 재부팅 직후 append 는 성공해야 */
        if (log_append(&ls, p, len) == NOR_OK) { g_acked[att] = 1; g_acked_seq[att] = ls.seq; appended_after++; }
        att++;
    }
    CHECK(cuts == 300 && inv_bad == 0 && lost == 0 && appended_after == cuts);
    printf("   [log] power-cut campaign: %u cuts, %u appends tried, torn records seen %u, rotate-after-mount %u, "
           "lost acked %u, bad/out-of-order %u\n", (unsigned)cuts, (unsigned)att, (unsigned)torn_seen,
           (unsigned)rot_after, (unsigned)lost, (unsigned)inv_bad);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
