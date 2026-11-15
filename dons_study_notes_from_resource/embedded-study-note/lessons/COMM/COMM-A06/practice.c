/* COMM-A06 추가 연습 — 한 번에 검증
 *
 * 본문 solution.c 의 mock SPI NOR 모델 + 드라이버(Part A/B)를 그대로 담고(로그 저장소는 뺌), 그 위에 연습 다섯 개:
 *   1) nor_update: 섹터 안 임의 바이트 바꾸기 (1→0 만이면 프로그램만, 아니면 read-modify-erase-write)
 *   2) erase_plan: 4K / 32K(0x52) / 64K 명령을 섞은 최소 명령 수 계획 — 256 KB 창의 모든 구간에서 DP 와 비교
 *   3) bp_for_protect: [from, size) 를 덮는 가장 작은 BP 값
 *   4) nor_wait_ready_cont: CS 를 잡은 채 RDSR 를 연속으로 읽어 WIP 대기 (버스 점유와 맞바꿈)
 *   5) nor_addr_frame: 16 MB 초과 칩의 4 바이트 주소 명령(0x13/0x12/0x21) 프레임
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
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

/* (Part A) chip_on_byte: 프레임의 idx 번째 바이트(0 = 명령)를 받는다. 반환 = 그 바이트 동안의 MISO.
 *   busy(WIP=1) 중 RDSR 이 아닌 명령은 프레임 전체를 무시(ignore) 하고 MISO = miso_idle.
 *   RDSR: 매 바이트 최신 SR.  RDID: id[0..2] 다음은 miso_idle.  WRSR: 1 번째 데이터 바이트를 wrsr_val 에.
 *   주소 명령(READ/PP/SE/BE): 1..3 번째 = 주소 MSB first (3 번째에서 size-1 로 마스크).
 *   READ: mem[(addr + k) & (size-1)] — 페이지 랩 없음.  PP: pbuf[(addr + k) & 0xFF] = mosi — 페이지 안 랩. */
uint8_t chip_on_byte(NorChip *c, uint32_t idx, uint8_t mosi) {
    if (idx == 0) {
        c->cmd = mosi;
        c->ignore = (c->sr & SR_WIP) && mosi != CMD_RDSR;          /* busy: RDSR 만 받는다 */
        if (c->ignore) c->n_ign_busy++;
        return c->miso_idle;
    }
    if (c->ignore) return c->miso_idle;
    if (c->cmd == CMD_RDSR) return c->sr;                           /* 계속 읽으면 계속 최신 값 */
    if (c->cmd == CMD_RDID) return idx <= 3 ? c->id[idx - 1] : c->miso_idle;
    if (c->cmd == CMD_WRSR) { if (idx == 1) c->wrsr_val = mosi; return c->miso_idle; }
    if (c->cmd != CMD_READ && c->cmd != CMD_PP && c->cmd != CMD_SE && c->cmd != CMD_BE) return c->miso_idle;
    if (idx <= 3) {
        c->addr = (c->addr << 8) | mosi;
        if (idx == 3) c->addr &= c->size - 1;                       /* 남는 상위 비트는 무시 (칩 크기로 랩) */
        return c->miso_idle;
    }
    uint32_t k = idx - 4;                                           /* 데이터 바이트 번호 */
    if (c->cmd == CMD_READ) return c->mem[(c->addr + k) & (c->size - 1)];
    if (c->cmd == CMD_PP) c->pbuf[(c->addr + k) & (NOR_PAGE - 1)] = mosi;   /* 같은 열이면 나중 값이 이긴다 */
    return c->miso_idle;
}

/* (Part A) chip_on_cs_rise: CS↑ 에서 명령 실행.
 *   무시된 프레임 / 빈 프레임 → 아무것도 안 함.  WREN → WEL=1 (drop_wren 장애면 무시), WRDI → WEL=0.
 *   WRSR(≥2 B) / PP(≥5 B) / SE·BE(정확히 4 B): 길이 틀림 → 무시, WEL=0 → 무시(n_ign_wel),
 *   WRSR 은 SRP=1 && WP# low → 무시(n_ign_prot), PP/SE/BE 는 대상 영역이 BP 보호 영역과 겹치면 무시(n_ign_prot).
 *   통과하면 chip_start_op (PP 는 pbuf 를 op_data 로 복사, base = 페이지/섹터/블록 정렬 주소). */
void chip_on_cs_rise(NorChip *c) {
    if (c->nbytes == 0 || c->ignore) return;
    switch (c->cmd) {
    case CMD_WREN: if (!c->drop_wren) c->sr |= SR_WEL; return;
    case CMD_WRDI: c->sr &= (uint8_t)~SR_WEL; return;
    case CMD_WRSR: case CMD_PP: case CMD_SE: case CMD_BE: break;
    default: return;                                                /* 읽기 명령은 CS↑ 에서 할 일 없음 */
    }
    bool len_ok = c->cmd == CMD_WRSR ? c->nbytes >= 2 : c->cmd == CMD_PP ? c->nbytes >= 5 : c->nbytes == 4;
    if (!len_ok) return;
    if (!(c->sr & SR_WEL)) { c->n_ign_wel++; return; }
    if (c->cmd == CMD_WRSR) {
        if ((c->sr & SR_SRP) && c->wp_low) { c->n_ign_prot++; return; }
        c->op_val = c->wrsr_val;
        chip_start_op(c, OP_WRSR, 0, T_W);
        return;
    }
    uint32_t span = c->cmd == CMD_PP ? NOR_PAGE : c->cmd == CMD_SE ? NOR_SECTOR : NOR_BLOCK;
    uint32_t base = c->addr & ~(span - 1);
    if (base + span > bp_protected_from(c->sr, c->size)) { c->n_ign_prot++; return; }
    if (c->cmd == CMD_PP) {
        memcpy(c->op_data, c->pbuf, NOR_PAGE);
        chip_start_op(c, OP_PP, base, T_PP);
    } else {
        chip_start_op(c, c->cmd == CMD_SE ? OP_SE : OP_BE, base, c->cmd == CMD_SE ? T_SE : T_BE);
    }
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

/* (Part A) nor_probe: JEDEC ID 를 읽어 size/vendor 를 채운다.
 *   ID 가 FF FF FF / 00 00 00 이면 "칩 없음" 또는 "MCU 리셋 때 진행 중이던 erase 가 아직 busy" 일 수 있다
 *   → nor_wait_ready(TO_BE, POLL_BE) 후 한 번 더 읽고, 그래도 그러면 E_NODEV.
 *   제조사 모름 → E_ID.  용량 코드 0x10..0x18 (64 KB..16 MB, 3 바이트 주소 한계) 밖 → E_ID.  size = 1 << 코드. */
int nor_probe(NorDev *d) {
    d->size = 0; d->vendor = NULL;
    nor_read_id(d, d->id);
    if (id_blank(d->id)) {
        if (nor_wait_ready(d, TO_BE, POLL_BE) != NOR_OK) return NOR_E_NODEV;
        nor_read_id(d, d->id);
        if (id_blank(d->id)) return NOR_E_NODEV;
    }
    d->vendor = nor_vendor_name(d->id[0]);
    if (!d->vendor) return NOR_E_ID;
    if (d->id[2] < 0x10 || d->id[2] > 0x18) return NOR_E_ID;
    d->size = 1u << d->id[2];
    return NOR_OK;
}

/* (Part A) nor_wait_ready: RDSR 를 읽어 WIP=0 이면 OK. 아니면 시작부터 timeout tick 이 지났으면 E_TIMEOUT,
 *   아니면 sim_delay(poll) 하고 다시. 경과 시간은 (uint32_t)(g_now - t0) 로 (랩어라운드 안전). */
int nor_wait_ready(NorDev *d, uint32_t timeout, uint32_t poll) {
    uint32_t t0 = g_now;
    for (;;) {
        if (!(nor_rdsr(d) & SR_WIP)) return NOR_OK;
        if ((uint32_t)(g_now - t0) >= timeout) return NOR_E_TIMEOUT;
        sim_delay(d, poll);
    }
}

/* (Part A) nor_write_enable: WREN 을 보내고 RDSR 로 확인. (WEL=1 이고 WIP=0) 이 아니면 E_WEL. */
int nor_write_enable(NorDev *d) {
    nor_cmd(d, CMD_WREN);
    uint8_t sr = nor_rdsr(d);
    return (sr & (SR_WEL | SR_WIP)) == SR_WEL ? NOR_OK : NOR_E_WEL;
}

/* (Part A) nor_write: [addr, addr+len) 에 프로그램 (1→0 만 — 지우기는 호출자 책임).
 *   인자/범위 → E_ARG.  len 0 → OK.  시작 전 RDSR: WIP → E_BUSY, 범위가 BP 보호 영역과 겹치면 E_PROTECTED.
 *   페이지 경계에서 자른다: 조각마다 write_enable → PP(주소 3 B + 데이터) → CS↑ → wait_ready(TO_PP, POLL_PP).
 *   어느 단계든 실패하면 그 코드를 바로 돌려준다 (다음 페이지를 보내지 않는다). */
int nor_write(NorDev *d, uint32_t addr, const uint8_t *buf, size_t len) {
    if (len && !buf) return NOR_E_ARG;
    if (addr > d->size || len > d->size - addr) return NOR_E_ARG;
    if (len == 0) return NOR_OK;
    uint8_t sr = nor_rdsr(d);
    if (sr & SR_WIP) return NOR_E_BUSY;
    if (addr + len > bp_protected_from(sr, d->size)) return NOR_E_PROTECTED;
    while (len > 0) {
        size_t n = NOR_PAGE - (addr & (NOR_PAGE - 1));          /* 이 페이지에 남은 칸 */
        if (n > len) n = len;
        int rc = nor_write_enable(d);
        if (rc != NOR_OK) return rc;
        spi_select(d); spi_byte(d, CMD_PP); nor_send_addr(d, addr);
        for (size_t i = 0; i < n; i++) spi_byte(d, buf[i]);
        spi_deselect(d);                                        /* 여기서 칩이 프로그램 시작 */
        rc = nor_wait_ready(d, TO_PP, POLL_PP);
        if (rc != NOR_OK) return rc;
        addr += (uint32_t)n; buf += n; len -= n;
    }
    return NOR_OK;
}

/* (Part B) nor_erase: [addr, addr+len) 을 0xFF 로. addr·len 이 4 KB 배수가 아니면 E_ALIGN, 범위 → E_ARG,
 *   len 0 → OK, WIP → E_BUSY, 보호 영역과 겹치면 E_PROTECTED.
 *   64 KB 정렬이고 64 KB 이상 남았으면 BE(0xD8, TO_BE/POLL_BE), 아니면 SE(0x20, TO_SE/POLL_SE). 매번 write_enable. */
int nor_erase(NorDev *d, uint32_t addr, uint32_t len) {
    if ((addr | len) & (NOR_SECTOR - 1)) return NOR_E_ALIGN;
    if (addr > d->size || len > d->size - addr) return NOR_E_ARG;
    if (len == 0) return NOR_OK;
    uint8_t sr = nor_rdsr(d);
    if (sr & SR_WIP) return NOR_E_BUSY;
    if (addr + len > bp_protected_from(sr, d->size)) return NOR_E_PROTECTED;
    while (len > 0) {
        bool blk = (addr & (NOR_BLOCK - 1)) == 0 && len >= NOR_BLOCK;
        int rc = nor_write_enable(d);
        if (rc != NOR_OK) return rc;
        spi_select(d); spi_byte(d, blk ? CMD_BE : CMD_SE); nor_send_addr(d, addr); spi_deselect(d);
        rc = nor_wait_ready(d, blk ? TO_BE : TO_SE, blk ? POLL_BE : POLL_SE);
        if (rc != NOR_OK) return rc;
        uint32_t step = blk ? NOR_BLOCK : NOR_SECTOR;
        addr += step; len -= step;
    }
    return NOR_OK;
}

/* (Part B) nor_unprotect: BP 비트를 0 으로. 이미 0 → OK.  WIP → E_BUSY.
 *   write_enable → WRSR(0x01, sr & ~BP & NV_MASK — SRP 등은 유지) → wait_ready(TO_W, POLL_W) → RDSR 재확인.
 *   BP 가 그대로면 (WP# + SRP 하드웨어 보호) WRDI 로 남은 WEL 을 지우고 E_PROTECTED. */
int nor_unprotect(NorDev *d) {
    uint8_t sr = nor_rdsr(d);
    if (sr & SR_WIP) return NOR_E_BUSY;
    if (!(sr & SR_BP_MASK)) return NOR_OK;
    int rc = nor_write_enable(d);
    if (rc != NOR_OK) return rc;
    spi_select(d); spi_byte(d, CMD_WRSR); spi_byte(d, (uint8_t)(sr & ~SR_BP_MASK & SR_NV_MASK)); spi_deselect(d);
    rc = nor_wait_ready(d, TO_W, POLL_W);
    if (rc != NOR_OK) return rc;
    if (nor_rdsr(d) & SR_BP_MASK) { nor_cmd(d, CMD_WRDI); return NOR_E_PROTECTED; }
    return NOR_OK;
}

/* ================= 추가 연습 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static NorChip g_chip;
static NorDev  g_dev;
static uint8_t g_ref[NOR_MAX_SIZE];
static uint8_t g_buf[8192];

static int fresh(void) {
    chip_init(&g_chip, NOR_MAX_SIZE, 0xEF, 0x40, 0x14);
    nor_init(&g_dev, &g_chip);
    return nor_probe(&g_dev);
}

/* ---- 연습 1: 1 바이트만 바꾸기 — read-modify-erase-write (1→0 만이면 erase 생략) ---- */
int nor_update(NorDev *d, uint32_t addr, const uint8_t *buf, size_t len, uint8_t *scratch, uint32_t *erases) {
    if (!buf || !scratch || len == 0) return NOR_E_ARG;
    if (addr > d->size || len > d->size - addr) return NOR_E_ARG;
    uint32_t sec = addr & ~(NOR_SECTOR - 1), off = addr - sec;
    if (off + len > NOR_SECTOR) return NOR_E_ARG;                       /* 섹터 하나 안에서만 */
    int rc = nor_read(d, sec, scratch, NOR_SECTOR);
    if (rc != NOR_OK) return rc;
    bool need_erase = false;
    for (size_t i = 0; i < len; i++)
        if ((scratch[off + i] & buf[i]) != buf[i]) { need_erase = true; break; }   /* 0→1 이 필요한 비트 */
    if (!need_erase) return nor_write(d, addr, buf, len);               /* AND 로 충분 */
    memcpy(scratch + off, buf, len);
    rc = nor_erase(d, sec, NOR_SECTOR);
    if (rc != NOR_OK) return rc;
    if (erases) (*erases)++;
    for (uint32_t p = 0; p < NOR_SECTOR; p += NOR_PAGE) {               /* 전부 0xFF 인 페이지는 건너뜀 */
        bool blank = true;
        for (uint32_t i = 0; i < NOR_PAGE && blank; i++) blank = scratch[p + i] == 0xFF;
        if (!blank && (rc = nor_write(d, sec + p, scratch + p, NOR_PAGE)) != NOR_OK) return rc;
    }
    return NOR_OK;
}

/* ---- 연습 2: erase 계획 — 4K(0x20) / 32K(0x52) / 64K(0xD8) 섞어 명령 수 최소 ---- */
typedef struct { uint8_t cmd; uint32_t addr; } EraseStep;
int erase_plan(uint32_t addr, uint32_t len, EraseStep *out, size_t cap) {
    if ((addr | len) & (NOR_SECTOR - 1)) return NOR_E_ALIGN;
    size_t n = 0;
    while (len > 0) {
        uint32_t step = (addr % 65536u == 0 && len >= 65536u) ? 65536u : (addr % 32768u == 0 && len >= 32768u) ? 32768u : 4096u;
        if (n >= cap) return NOR_E_ARG;
        out[n].cmd = step == 65536u ? 0xD8 : step == 32768u ? 0x52 : 0x20;
        out[n].addr = addr; n++;
        addr += step; len -= step;
    }
    return (int)n;
}
static int dp_min_erase(uint32_t s0, uint32_t s1) {                     /* 섹터 [s0, s1) 를 지우는 최소 명령 수 */
    int best[129];
    best[0] = 0;
    for (uint32_t i = 1; i <= s1 - s0; i++) {
        uint32_t end = s0 + i;
        best[i] = best[i - 1] + 1;
        if (i >= 8 && (end - 8) % 8 == 0 && best[i - 8] + 1 < best[i]) best[i] = best[i - 8] + 1;
        if (i >= 16 && (end - 16) % 16 == 0 && best[i - 16] + 1 < best[i]) best[i] = best[i - 16] + 1;
    }
    return best[s1 - s0];
}

/* ---- 연습 3: [from, size) 를 보호하는 가장 작은 BP ---- */
int bp_for_protect(uint32_t from, uint32_t size) {
    for (int bp = 0; bp < 8; bp++)
        if (bp_protected_from((uint8_t)(bp << 2), size) <= from) return bp;
    return -1;
}

/* ---- 연습 4: CS 를 잡은 채 RDSR 연속 읽기 ---- */
int nor_wait_ready_cont(NorDev *d, uint32_t timeout, uint32_t *bytes) {
    uint32_t t0 = g_now, n = 0;
    int rc = NOR_E_TIMEOUT;
    spi_select(d); spi_byte(d, CMD_RDSR);
    for (;;) {
        uint8_t v = spi_byte(d, 0xFF); n++;
        if (!(v & SR_WIP)) { rc = NOR_OK; break; }
        if ((uint32_t)(g_now - t0) >= timeout) break;
    }
    spi_deselect(d);
    if (bytes) *bytes = n;
    return rc;
}

/* ---- 연습 5: 3 바이트 / 4 바이트 주소 프레임 만들기 ---- */
int nor_addr_frame(char op, uint32_t addr, uint32_t size, uint8_t *out, size_t cap) {
    static const uint8_t op3[3] = { CMD_READ, CMD_PP, CMD_SE }, op4[3] = { 0x13, 0x12, 0x21 };
    int k = op == 'R' ? 0 : op == 'P' ? 1 : op == 'E' ? 2 : -1;
    if (k < 0 || addr >= size || !out) return NOR_E_ARG;
    bool four = size > (1u << 24);
    size_t n = four ? 5 : 4;
    if (cap < n) return NOR_E_ARG;
    out[0] = four ? op4[k] : op3[k];
    for (size_t i = 1; i < n; i++) out[i] = (uint8_t)(addr >> (8 * (n - 1 - i)));
    return (int)n;
}

int main(void) {
    uint8_t scratch[NOR_SECTOR];
    uint32_t er = 0;
    printf("-- 연습 1: nor_update\n");
    CHECK(fresh() == NOR_OK);
    uint8_t v1[3] = { 0x12, 0x34, 0x56 };
    CHECK(nor_update(&g_dev, 0x3010, v1, 3, scratch, &er) == NOR_OK && er == 0);       /* 빈 칸 → 프로그램만 */
    uint8_t v2[1] = { 0x10 };
    CHECK(nor_update(&g_dev, 0x3010, v2, 1, scratch, &er) == NOR_OK && er == 0);       /* 0x12 → 0x10: 1→0 만 */
    uint8_t v3[1] = { 0xFF };
    memset(g_chip.mem + 0x3800, 0x77, 100);
    CHECK(nor_update(&g_dev, 0x3011, v3, 1, scratch, &er) == NOR_OK && er == 1);       /* 0x34 → 0xFF: erase 필요 */
    CHECK(g_chip.mem[0x3010] == 0x10 && g_chip.mem[0x3011] == 0xFF && g_chip.mem[0x3012] == 0x56 && g_chip.mem[0x3863] == 0x77);
    CHECK(nor_update(&g_dev, 0x3FFF, v1, 2, scratch, &er) == NOR_E_ARG);              /* 섹터 경계 넘음 */
    CHECK(fresh() == NOR_OK);                                                           /* 기준 모델과 같은 출발점 */
    memset(g_ref, 0xFF, sizeof g_ref);
    uint32_t s = 0xA11CEu, ops = 0, bad = 0;
    er = 0;
    for (int i = 0; i < 600; i++) {
        uint32_t sec = (xs32(&s) % 8u) * NOR_SECTOR, off = xs32(&s) % NOR_SECTOR, n = 1 + xs32(&s) % 64u;
        if (off + n > NOR_SECTOR) n = NOR_SECTOR - off;
        for (uint32_t j = 0; j < n; j++) g_buf[j] = (xs32(&s) % 3 == 0) ? (uint8_t)(g_ref[sec + off + j] & xs32(&s)) : (uint8_t)xs32(&s);
        memcpy(g_ref + sec + off, g_buf, n);
        bad += nor_update(&g_dev, sec + off, g_buf, n, scratch, &er) != NOR_OK; ops++;
    }
    CHECK(bad == 0 && memcmp(g_chip.mem, g_ref, 8 * NOR_SECTOR) == 0 && er > 0 && er < ops);
    printf("   600 random updates: %u erases (%u skipped because only 1->0 was needed)\n", (unsigned)er, (unsigned)(ops - er));

    printf("-- 연습 2: erase_plan (4K/32K/64K) vs DP 최소\n");
    EraseStep st[64];
    int worse = 0, cover_bad = 0, ranges = 0;
    for (uint32_t s0 = 0; s0 < 64; s0++)
        for (uint32_t s1 = s0 + 1; s1 <= 64; s1++) {
            int n = erase_plan(s0 * NOR_SECTOR, (s1 - s0) * NOR_SECTOR, st, 64);
            uint32_t a = s0 * NOR_SECTOR;
            for (int k = 0; k < n; k++) {
                uint32_t sz = st[k].cmd == 0xD8 ? 65536u : st[k].cmd == 0x52 ? 32768u : 4096u;
                if (st[k].addr != a || st[k].addr % sz) cover_bad++;
                a += sz;
            }
            if (n < 0 || a != s1 * NOR_SECTOR) cover_bad++;
            if (n != dp_min_erase(s0, s1)) worse++;
            ranges++;
        }
    CHECK(ranges == 2080 && cover_bad == 0 && worse == 0);
    int n1 = erase_plan(0x7000, 0x1A000, st, 64);
    CHECK(n1 == 4 && st[0].cmd == 0x20 && st[1].cmd == 0x52 && st[2].cmd == 0xD8 && st[3].cmd == 0x20);
    printf("   0x7000+0x1A000 -> %d steps: SE@%05X  32K@%05X  BE@%05X  SE@%05X\n", n1, (unsigned)st[0].addr,
           (unsigned)st[1].addr, (unsigned)st[2].addr, (unsigned)st[3].addr);
    CHECK(erase_plan(0x1000, 0x10000, st, 2) == NOR_E_ARG && erase_plan(0x1001, 4096, st, 4) == NOR_E_ALIGN);

    printf("-- 연습 3: bp_for_protect\n");
    int minimal = 1, covers = 1;
    for (uint32_t from = 0; from <= NOR_MAX_SIZE; from += NOR_SECTOR) {
        int bp = bp_for_protect(from, NOR_MAX_SIZE);
        if (bp < 0 || bp_protected_from((uint8_t)(bp << 2), NOR_MAX_SIZE) > from) covers = 0;
        if (bp > 0 && bp_protected_from((uint8_t)((bp - 1) << 2), NOR_MAX_SIZE) <= from) minimal = 0;
    }
    CHECK(covers && minimal);
    CHECK(bp_for_protect(NOR_MAX_SIZE, NOR_MAX_SIZE) == 0 && bp_for_protect(0xF0000, NOR_MAX_SIZE) == 1);
    CHECK(bp_for_protect(0xEF000, NOR_MAX_SIZE) == 2 && bp_for_protect(0x80000, NOR_MAX_SIZE) == 4 && bp_for_protect(0x7F000, NOR_MAX_SIZE) == 5);
    printf("   protect [0xEF000, 1M) -> BP=2 protects from 0x%05X (%u KB more than asked)\n",
           (unsigned)bp_protected_from(2 << 2, NOR_MAX_SIZE), (unsigned)((0xEF000 - bp_protected_from(2 << 2, NOR_MAX_SIZE)) / 1024));

    printf("-- 연습 4: CS 를 잡은 연속 RDSR\n");
    CHECK(fresh() == NOR_OK);
    uint32_t bytes = 0;
    CHECK(nor_write_enable(&g_dev) == NOR_OK);
    spi_select(&g_dev); spi_byte(&g_dev, CMD_PP); nor_send_addr(&g_dev, 0); spi_byte(&g_dev, 0x00); spi_deselect(&g_dev);
    CHECK(nor_wait_ready_cont(&g_dev, TO_PP, &bytes) == NOR_OK && nor_rdsr(&g_dev) == 0 && g_chip.mem[0] == 0);
    CHECK(bytes >= T_PP - 2 && bytes <= T_PP + 2);
    printf("   one CS frame: %u status bytes for t_PP=%u us (bus held the whole time)\n", (unsigned)bytes, T_PP);
    g_chip.stuck_busy = true;
    CHECK(nor_write_enable(&g_dev) == NOR_OK);
    spi_select(&g_dev); spi_byte(&g_dev, CMD_PP); nor_send_addr(&g_dev, 4); spi_byte(&g_dev, 0x00); spi_deselect(&g_dev);
    CHECK(nor_wait_ready_cont(&g_dev, 500, &bytes) == NOR_E_TIMEOUT && !g_chip.cs_low && bytes >= 498 && bytes <= 502);

    printf("-- 연습 5: 4 바이트 주소 프레임\n");
    uint8_t fr[8];
    CHECK(nor_addr_frame('R', 0xFFFFFF, 1u << 24, fr, 8) == 4 && fr[0] == 0x03 && fr[1] == 0xFF && fr[3] == 0xFF);
    CHECK(nor_addr_frame('R', 0x1000000, 1u << 24, fr, 8) == NOR_E_ARG);
    CHECK(nor_addr_frame('P', 0x1000000, 1u << 25, fr, 8) == 5 && fr[0] == 0x12 && fr[1] == 0x01 && fr[2] == 0 && fr[4] == 0);
    CHECK(nor_addr_frame('E', 0x0123000, 1u << 25, fr, 8) == 5 && fr[0] == 0x21 && fr[2] == 0x12 && fr[3] == 0x30);
    CHECK(nor_addr_frame('E', 0x0F3000, 1u << 20, fr, 8) == 4 && fr[0] == 0x20 && fr[1] == 0x0F && fr[2] == 0x30);
    CHECK(nor_addr_frame('P', 0, 1u << 25, fr, 4) == NOR_E_ARG && nor_addr_frame('X', 0, 1u << 20, fr, 8) == NOR_E_ARG);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
