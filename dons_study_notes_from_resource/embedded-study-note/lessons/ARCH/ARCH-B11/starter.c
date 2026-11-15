/* ARCH-B11 메모리 맵과 주소 디코더 모델 — 체크 테스트
 *
 *   Part A  메모리 맵 테이블 + 디코더: map_find, bus_check (정책 검사 순서), SRAM/Flash 디바이스 콜백
 *           (리틀 엔디언), GPIO/UART 레지스터 블록으로 라우팅
 *   Part B  경계·함정: 맵 검증(0 크기 / 4 GB 넘김 / 겹침), 영역 끝 걸침, 주소 공간 끝(0xFFFF_FFFF),
 *           0x0 부트 alias, 영역별 폭·정렬 정책, 독립 참조 모델과 경계 스윕 + 무작위 비교
 *   Part C  펌웨어 현실: BFAR 비슷한 폴트 기록, posted write(쓰기 버퍼) → imprecise 폴트,
 *           TXE 폴링 + 타임아웃 UART 드라이버, 클럭 게이트 정책 (RAZ/WI vs 폴트)
 *
 * 맵은 STM32 스타일(STM32F4 주소 일부를 빌림)이지만 크기·정책은 모델용으로 단순화했다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 공통 타입 (주어짐) ======================= */
enum { SZ1 = 1u << 0, SZ2 = 1u << 1, SZ4 = 1u << 2 };            /* 허용 접근 폭 마스크 */
static unsigned size_bit(unsigned size) { return size == 1 ? SZ1 : size == 2 ? SZ2 : size == 4 ? SZ4 : 0; }

typedef enum { GATE_NONE, GATE_RAZWI, GATE_FAULT } gate_t;      /* 클럭이 꺼졌을 때의 동작 */
typedef enum { BUS_OK = 0, F_UNMAPPED, F_SIZE, F_UNALIGNED, F_CROSS, F_RO, F_GATED, F_SLVERR } bus_st_t;

/* 디바이스 콜백: off = 디바이스 안 오프셋. 0 = OK, 0 이 아니면 slave error (AHB HRESP=ERROR 비슷) */
typedef int (*rd_fn)(void *ctx, uint32_t off, unsigned size, uint32_t *val);
typedef int (*wr_fn)(void *ctx, uint32_t off, unsigned size, uint32_t val);

typedef struct {
    const char *name;
    uint32_t base, size;     /* [base, base+size) — base+size 는 정확히 2^32 까지 허용 */
    uint8_t  sizes;          /* SZ1|SZ2|SZ4 중 허용 폭 */
    bool     align;          /* true: 자연 정렬 필수 (주변장치) */
    bool     writable;
    gate_t   gate;
    bool     clk_on;
    rd_fn rd; wr_fn wr; void *ctx;
    uint32_t ctx_off;        /* 디바이스 오프셋 = (addr - base) + ctx_off   (alias 용) */
} region_t;

typedef struct {
    uint32_t addr; unsigned size; bool write;
    bus_st_t why; int region;        /* region: 디코드된 영역 인덱스, 미매핑이면 -1 */
    bool precise;                    /* false = 쓰기 버퍼에서 나중에 터진 폴트 */
} fault_t;

typedef struct {
    region_t *map; int n;
    uint32_t nfault; fault_t last;   /* 폴트 수 + 마지막 폴트 */
    bool bfar_valid; uint32_t bfar;  /* BFARVALID / BFAR 흉내: 첫 precise 폴트 주소, 지울 때까지 유지 */
    bool wb_full; uint32_t wb_addr, wb_val; unsigned wb_size;   /* 깊이 1 쓰기 버퍼 (Part C) */
} bus_t;

/* ======================= Part A — 디바이스 콜백과 디코더 ======================= */
typedef struct { uint8_t *p; uint32_t len; } mem_t;             /* SRAM / Flash 배열 */

/* 리틀 엔디언: 바이트 off 가 값의 비트 7..0. off+size 가 배열을 넘으면 slave error(1). */
static int mem_rd(void *ctx, uint32_t off, unsigned size, uint32_t *val) {
    (void)ctx; (void)off; (void)size;
    *val = 0;
    return 0; /* TODO: 범위 검사 후 바이트 off..off+size-1 을 리틀 엔디언으로 모은다 */
}
static int mem_wr(void *ctx, uint32_t off, unsigned size, uint32_t val) {
    (void)ctx; (void)off; (void)size; (void)val;
    return 0; /* TODO: 범위 검사 후 val 의 하위 바이트부터 p[off] 에 쓴다 */
}

/* addr 를 포함하는 영역 인덱스, 없으면 -1. 영역 끝이 2^32 여도 맞아야 한다. */
static int map_find(const bus_t *b, uint32_t addr) {
    (void)b; (void)addr;
    return -1; /* TODO: base <= addr < base+size 인 영역 (2^32 끝 주의) */
}

/* 디코드 + 정책 검사. 순서 고정:
 *   ① 미매핑 → F_UNMAPPED  ② 폭 불허(1/2/4 아님 포함) → F_SIZE  ③ align 영역에서 비정렬 → F_UNALIGNED
 *   ④ 마지막 바이트가 영역 밖 → F_CROSS  ⑤ 읽기 전용에 쓰기 → F_RO  ⑥ 클럭 꺼짐 + GATE_FAULT → F_GATED
 * *idx 에는 디코드된 영역(없으면 -1). */
static bus_st_t bus_check(const bus_t *b, uint32_t addr, unsigned size, bool write, int *idx) {
    (void)b; (void)addr; (void)size; (void)write; (void)size_bit;
    *idx = -1;
    return F_UNMAPPED; /* TODO: map_find → ①..⑥ 순서로 검사 */
}

/* ---- 버스 접근 (주어짐): 검사 → 게이트 → 디바이스 콜백 → 폴트 기록 ---- */
static void fault_record(bus_t *b, const fault_t *f);   /* Part C */
static bus_st_t bus_drain(bus_t *b);                     /* Part C */

static bus_st_t do_access(bus_t *b, uint32_t addr, unsigned size, bool write, uint32_t *val, bool precise) {
    int i;
    bus_st_t st = bus_check(b, addr, size, write, &i);
    if (st == BUS_OK) {
        const region_t *r = &b->map[i];
        uint32_t off = addr - r->base + r->ctx_off;
        if (r->gate == GATE_RAZWI && !r->clk_on) {          /* 조용히 0 으로 읽히고 쓰기는 무시 */
            if (!write) *val = 0;
            return BUS_OK;
        }
        int e = write ? r->wr(r->ctx, off, size, *val) : r->rd(r->ctx, off, size, val);
        if (e) st = F_SLVERR;
    }
    if (st != BUS_OK) {
        fault_t f = { addr, size, write, st, i, precise };
        fault_record(b, &f);
    }
    return st;
}
static bus_st_t bus_read(bus_t *b, uint32_t addr, unsigned size, uint32_t *val) {
    *val = 0;
    bus_drain(b);                    /* 읽기 전에 쓰기 버퍼를 비운다 (프로그램 순서 유지) */
    return do_access(b, addr, size, false, val, true);
}
static bus_st_t bus_write(bus_t *b, uint32_t addr, unsigned size, uint32_t val) {
    bus_drain(b);
    return do_access(b, addr, size, true, &val, true);
}

/* ======================= Part B — 맵 검증 ======================= */
typedef enum { MAP_OK, MAP_ZERO, MAP_WRAP, MAP_OVERLAP } map_err_t;

/* 0 크기 → MAP_ZERO, base+size > 2^32 → MAP_WRAP (영역별로 먼저 전부 검사), 그다음 어떤 두 영역이든
 * 바이트가 겹치면 MAP_OVERLAP. *bad = 문제 영역 인덱스 (겹침이면 뒤쪽 j), OK 면 -1. 정렬돼 있다고 가정하지 않는다. */
static map_err_t map_validate(const region_t *m, int n, int *bad) {
    (void)m; (void)n;
    *bad = -1;
    return MAP_OK; /* TODO: 0 크기 / 2^32 넘김 (영역별) → 겹침 (모든 쌍, 64비트 산술) */
}

/* ======================= Part C — 폴트 기록, 쓰기 버퍼, UART 드라이버 ======================= */
/* 폴트 수를 세고 마지막 폴트를 저장. precise 폴트이고 bfar_valid 가 꺼져 있을 때만 bfar 를 잡는다
 * (첫 폴트 보존). imprecise 폴트는 주소를 믿을 수 없으므로 bfar 를 건드리지 않는다. */
static void fault_record(bus_t *b, const fault_t *f) {
    (void)b; (void)f; /* TODO: nfault++, last 저장, precise 이고 bfar_valid 가 꺼져 있으면 bfar 잡기 */
}
static void fault_clear(bus_t *b) { b->bfar_valid = false; b->bfar = 0; }   /* 핸들러가 W1C 로 지우는 것 */

/* 버퍼에 쓰기가 있으면 지금 버스로 내보낸다. 폴트는 imprecise 로 기록된다. */
static bus_st_t bus_drain(bus_t *b) {
    (void)b;
    return BUS_OK; /* TODO: 버퍼가 차 있으면 비우고 do_access(..., write, precise=false) */
}
/* posted write: 버퍼(깊이 1)에 넣고 바로 BUS_OK. 버퍼가 차 있으면 먼저 drain. */
static bus_st_t bus_write_posted(bus_t *b, uint32_t addr, unsigned size, uint32_t val) {
    (void)b; (void)addr; (void)size; (void)val;
    return BUS_OK; /* TODO: 차 있으면 drain, 버퍼에 넣고 바로 OK */
}

/* ---- UART 모델 (주어짐, STM32F4 USART 레지스터 배치를 단순화) ---- */
#define UART_SR  0x00u
#define UART_DR  0x04u
#define UART_BRR 0x08u
#define UART_CR1 0x0Cu
#define SR_RXNE  (1u << 5)
#define SR_TXE   (1u << 7)
#define UART_BUSY_POLLS 2            /* DR 쓰기 후 SR 읽기 2번 동안 TXE=0 (전송 중) */

typedef struct { uint32_t brr, cr1; int busy; bool rxne; uint8_t rx; char tx[32]; int ntx; int lost; } uart_t;

static int uart_rd(void *ctx, uint32_t off, unsigned size, uint32_t *val) {
    uart_t *u = ctx; (void)size;
    switch (off) {
    case UART_SR:  *val = (u->busy > 0 ? 0u : SR_TXE) | (u->rxne ? SR_RXNE : 0u);
                   if (u->busy > 0) u->busy--;
                   break;
    case UART_DR:  *val = u->rx; u->rxne = false; break;       /* DR 읽기가 RXNE 를 지운다 */
    case UART_BRR: *val = u->brr; break;
    case UART_CR1: *val = u->cr1; break;
    default:       *val = 0; break;                            /* 예약 오프셋: RAZ */
    }
    return 0;
}
static int uart_wr(void *ctx, uint32_t off, unsigned size, uint32_t val) {
    uart_t *u = ctx; (void)size;
    switch (off) {
    case UART_DR:
        if (u->busy > 0) { u->lost++; break; }                 /* TXE=0 인데 썼다 → 바이트 유실 */
        if (u->ntx < (int)sizeof u->tx - 1) u->tx[u->ntx++] = (char)(val & 0xFFu);
        u->busy = UART_BUSY_POLLS;
        break;
    case UART_BRR: u->brr = val; break;
    case UART_CR1: u->cr1 = val; break;
    default: break;                                            /* SR·예약: 쓰기 무시 (WI) */
    }
    return 0;
}

/* SR.TXE 를 최대 max_polls 번 폴링한 뒤 DR 에 쓴다 (4바이트 접근).
 * 반환: 0 = 보냄, -1 = 버스 폴트 (SR 읽기 또는 DR 쓰기), -2 = 타임아웃 */
static int uart_putc(bus_t *b, uint32_t base, char c, int max_polls) {
    (void)b; (void)base; (void)c; (void)max_polls;
    return -2; /* TODO: SR.TXE 폴링 (최대 max_polls), DR 에 4바이트 쓰기, 폴트면 -1 */
}

/* ---- GPIO 모델 (주어짐, STM32F4 GPIO 오프셋 일부: MODER/IDR/ODR/BSRR, 바이트·하프워드 레인 지원) ---- */
#define GPIO_MODER 0x00u
#define GPIO_IDR   0x10u
#define GPIO_ODR   0x14u
#define GPIO_BSRR  0x18u
typedef struct { uint32_t moder, idr, odr; } gpio_t;

static uint32_t *gpio_reg(gpio_t *g, uint32_t w) {
    return w == GPIO_MODER ? &g->moder : w == GPIO_IDR ? &g->idr : w == GPIO_ODR ? &g->odr : NULL;
}
static uint32_t lane_mask(unsigned size) { return size == 4 ? 0xFFFFFFFFu : (1u << (8u * size)) - 1u; }

static int gpio_rd(void *ctx, uint32_t off, unsigned size, uint32_t *val) {
    gpio_t *g = ctx;
    uint32_t w = off & ~3u, sh = 8u * (off & 3u);
    uint32_t *r = gpio_reg(g, w);
    *val = ((r ? *r : 0u) >> sh) & lane_mask(size);           /* BSRR·예약 영역은 0 으로 읽힌다 */
    return 0;
}
static int gpio_wr(void *ctx, uint32_t off, unsigned size, uint32_t val) {
    gpio_t *g = ctx;
    uint32_t w = off & ~3u, sh = 8u * (off & 3u);
    uint32_t m = lane_mask(size) << sh, lane = (val << sh) & m;
    if (w == GPIO_BSRR) { g->odr = (g->odr & ~(lane >> 16)) | (lane & 0xFFFFu); return 0; }
    if (w == GPIO_IDR) return 0;                               /* 읽기 전용 → 쓰기 무시 */
    uint32_t *r = gpio_reg(g, w);
    if (r) *r = (*r & ~m) | lane;
    return 0;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint8_t flash_mem[0x10000], sram_mem[0x4000], top_mem[0x1000];
static mem_t flash = { flash_mem, sizeof flash_mem }, sram = { sram_mem, sizeof sram_mem };
static mem_t topm = { top_mem, sizeof top_mem };
static uart_t usart2;
static gpio_t gpioa;

enum { R_BOOT, R_FLASH, R_SRAM, R_USART2, R_GPIOA, R_N };
static region_t stm32_map[R_N] = {
    /* name      base         size      sizes        align  wr     gate        clk   rd       wr       ctx      ctx_off */
    { "BOOT",   0x00000000u, 0x10000u, SZ1|SZ2|SZ4, false, false, GATE_NONE,  true, mem_rd,  mem_wr,  &flash,  0 },
    { "FLASH",  0x08000000u, 0x10000u, SZ1|SZ2|SZ4, false, false, GATE_NONE,  true, mem_rd,  mem_wr,  &flash,  0 },
    { "SRAM",   0x20000000u, 0x04000u, SZ1|SZ2|SZ4, false, true,  GATE_NONE,  true, mem_rd,  mem_wr,  &sram,   0 },
    { "USART2", 0x40004400u, 0x00400u, SZ2|SZ4,     true,  true,  GATE_RAZWI, true, uart_rd, uart_wr, &usart2, 0 },
    { "GPIOA",  0x40020000u, 0x00400u, SZ1|SZ2|SZ4, true,  true,  GATE_RAZWI, true, gpio_rd, gpio_wr, &gpioa,  0 },
};

/* 독립 참조 모델: 64비트 산술 + 바이트 단위 판정 (테스트 전용, 다른 방식으로 같은 규칙) */
static bus_st_t ref_check(const region_t *m, int n, uint32_t addr, unsigned size, bool write) {
    const region_t *r = NULL;
    for (int i = n - 1; i >= 0; i--)
        if ((uint64_t)addr >= m[i].base && (uint64_t)addr < (uint64_t)m[i].base + m[i].size) r = &m[i];
    if (!r) return F_UNMAPPED;
    if (size != 1 && size != 2 && size != 4) return F_SIZE;
    if (!(r->sizes & (size == 4 ? 4u : size))) return F_SIZE;
    if (r->align && addr % size != 0) return F_UNALIGNED;
    for (unsigned k = 1; k < size; k++)
        if ((uint64_t)addr + k >= (uint64_t)r->base + r->size) return F_CROSS;
    if (write && !r->writable) return F_RO;
    if (r->gate == GATE_FAULT && !r->clk_on) return F_GATED;
    return BUS_OK;
}
static uint32_t xs_state = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}
static int sweep_mismatches(bus_t *b) {
    int bad = 0;
    for (int ri = 0; ri < b->n; ri++) {
        uint64_t edges[2] = { b->map[ri].base, (uint64_t)b->map[ri].base + b->map[ri].size };
        for (int e = 0; e < 2; e++)
            for (int d = -16; d <= 16; d++)
                for (unsigned sz = 0; sz <= 5; sz++)
                    for (int w = 0; w < 2; w++) {
                        uint32_t a = (uint32_t)(edges[e] + (uint64_t)(int64_t)d);   /* 2^32 근처는 감김 */
                        int idx;
                        if (bus_check(b, a, sz, w, &idx) != ref_check(b->map, b->n, a, sz, w)) bad++;
                    }
    }
    for (int k = 0; k < 200000; k++) {
        uint32_t r = xorshift32(), a;
        const region_t *rg = &b->map[r % (uint32_t)b->n];
        a = (k & 1) ? xorshift32() : rg->base + (xorshift32() % (rg->size + 64u)) - 32u;
        unsigned sz = (unsigned)(r >> 8) % 6u;
        bool w = (r >> 16) & 1u;
        int idx;
        if (bus_check(b, a, sz, w, &idx) != ref_check(b->map, b->n, a, sz, w)) bad++;
    }
    return bad;
}

static bus_t new_bus(region_t *m, int n) { bus_t b; memset(&b, 0, sizeof b); b.map = m; b.n = n; return b; }

int main(void) {
    bus_t bus = new_bus(stm32_map, R_N);
    uint32_t v;
    int idx;

    /* 플래시 이미지: 벡터 테이블 [0] = 초기 SP (SRAM 꼭대기), [1] = Reset_Handler (Thumb 비트 1) */
    const uint8_t vec[8] = { 0x00, 0x40, 0x00, 0x20, 0x01, 0x01, 0x00, 0x08 };
    memcpy(flash_mem, vec, sizeof vec);

    /* ---------------- Part A: 디코드와 라우팅 ---------------- */
    CHECK(map_find(&bus, 0x08000000u) == R_FLASH);
    CHECK(map_find(&bus, 0x0800FFFFu) == R_FLASH);
    CHECK(map_find(&bus, 0x08010000u) == -1);
    CHECK(map_find(&bus, 0x20003FFFu) == R_SRAM);
    CHECK(map_find(&bus, 0x40004400u) == R_USART2);
    CHECK(map_find(&bus, 0x400043FFu) == -1);
    CHECK(map_find(&bus, 0x40020014u) == R_GPIOA);
    CHECK(map_find(&bus, 0x1FFFFFFFu) == -1);

    CHECK(bus_read(&bus, 0x08000000u, 4, &v) == BUS_OK && v == 0x20004000u);
    CHECK(bus_write(&bus, 0x20000010u, 4, 0x11223344u) == BUS_OK);
    CHECK(bus_read(&bus, 0x20000010u, 1, &v) == BUS_OK && v == 0x44u);          /* 리틀 엔디언 */
    CHECK(bus_read(&bus, 0x20000012u, 2, &v) == BUS_OK && v == 0x1122u);
    CHECK(sram_mem[0x13] == 0x11u);

    CHECK(bus_write(&bus, 0x40020000u + GPIO_MODER, 4, 1u << 10) == BUS_OK && gpioa.moder == (1u << 10));
    CHECK(bus_write(&bus, 0x40020000u + GPIO_BSRR, 4, 1u << 5) == BUS_OK && gpioa.odr == (1u << 5));
    CHECK(bus_read(&bus, 0x40020000u + GPIO_ODR, 4, &v) == BUS_OK && v == (1u << 5));
    CHECK(bus_write(&bus, 0x40004400u + UART_BRR, 4, 0x683u) == BUS_OK && usart2.brr == 0x683u);

    CHECK(bus_read(&bus, 0x30000000u, 4, &v) == F_UNMAPPED && v == 0);
    CHECK(bus_check(&bus, 0x08000000u, 4, true, &idx) == F_RO && idx == R_FLASH);
    CHECK(bus_check(&bus, 0x40004400u, 1, false, &idx) == F_SIZE && idx == R_USART2);   /* UART 바이트 접근 불허 */

    /* ---------------- Part B: 경계와 함정 ---------------- */
    int bad;
    CHECK(map_validate(stm32_map, R_N, &bad) == MAP_OK && bad == -1);
    {
        region_t m[3] = { stm32_map[R_SRAM], stm32_map[R_SRAM], stm32_map[R_GPIOA] };
        m[1].base = 0x20004000u;                                          /* 딱 붙어 있음 → OK */
        CHECK(map_validate(m, 3, &bad) == MAP_OK);
        m[1].base = 0x20003FFCu;                                          /* 4바이트 겹침 */
        CHECK(map_validate(m, 3, &bad) == MAP_OVERLAP && bad == 1);
        m[1] = m[2]; m[1].base = 0x40020200u;                             /* 순서가 섞인 겹침 */
        CHECK(map_validate(m, 3, &bad) == MAP_OVERLAP && bad == 2);
        m[1].size = 0;
        CHECK(map_validate(m, 3, &bad) == MAP_ZERO && bad == 1);
        m[1].base = 0xFFFFF000u; m[1].size = 0x1000u;                     /* 끝 = 정확히 2^32 → OK */
        CHECK(map_validate(m, 3, &bad) == MAP_OK);
        m[1].size = 0x2000u;                                              /* 2^32 를 넘음 */
        CHECK(map_validate(m, 3, &bad) == MAP_WRAP && bad == 1);
    }

    CHECK(bus_write(&bus, 0x20000014u, 4, 0xAABBCCDDu) == BUS_OK);
    CHECK(bus_read(&bus, 0x20000011u, 4, &v) == BUS_OK && v == 0xDD112233u);     /* SRAM 비정렬 OK */
    CHECK(bus_read(&bus, 0x20003FFCu, 4, &v) == BUS_OK);
    CHECK(bus_read(&bus, 0x20003FFFu, 1, &v) == BUS_OK);
    CHECK(bus_read(&bus, 0x20003FFEu, 4, &v) == F_CROSS);                         /* 영역 끝을 넘김 */
    CHECK(bus_check(&bus, 0x40020015u, 2, false, &idx) == F_UNALIGNED);           /* 주변장치는 정렬 필수 */
    CHECK(bus_check(&bus, 0x40020016u, 2, false, &idx) == BUS_OK);
    CHECK(bus_check(&bus, 0x40020001u, 3, false, &idx) == F_SIZE);                /* 폭 검사가 정렬보다 먼저 */
    CHECK(bus_check(&bus, 0x30000001u, 3, false, &idx) == F_UNMAPPED && idx == -1);/* 디코드가 제일 먼저 */

    CHECK(bus_read(&bus, 0x00000004u, 4, &v) == BUS_OK && v == 0x08000101u);     /* 0x0 alias → Flash */
    CHECK(bus_write(&bus, 0x00000004u, 4, 0) == F_RO);
    {
        region_t m2[R_N + 1];
        memcpy(m2, stm32_map, sizeof stm32_map);
        m2[R_N] = stm32_map[R_SRAM];                                      /* SRAM alias, 설정 실수 */
        m2[R_N].name = "SRAM_ALIAS"; m2[R_N].base = 0x10000000u; m2[R_N].size = 0x8000u;
        bus_t b2 = new_bus(m2, R_N + 1);
        CHECK(bus_read(&b2, 0x10000010u, 4, &v) == BUS_OK && v == 0x11223344u);  /* 같은 SRAM 이 보인다 */
        CHECK(bus_read(&b2, 0x10004000u, 4, &v) == F_SLVERR);             /* 디코더는 OK, 디바이스가 거부 */
    }
    {
        region_t top[1] = { { "TOP", 0xFFFFF000u, 0x1000u, SZ1|SZ2|SZ4, false, true, GATE_NONE, true,
                              mem_rd, mem_wr, &topm, 0 } };
        bus_t bt = new_bus(top, 1);
        CHECK(map_find(&bt, 0xFFFFFFFFu) == 0 && map_find(&bt, 0xFFFFEFFFu) == -1);
        CHECK(bus_write(&bt, 0xFFFFFFFCu, 4, 0xCAFEF00Du) == BUS_OK && top_mem[0xFFF] == 0xCAu);
        CHECK(bus_read(&bt, 0xFFFFFFFEu, 4, &v) == F_CROSS);              /* 0 으로 감기면 안 된다 */
    }
    {
        region_t m3[R_N];
        memcpy(m3, stm32_map, sizeof stm32_map);
        m3[R_USART2].gate = GATE_FAULT; m3[R_USART2].clk_on = false;
        bus_t b3 = new_bus(m3, R_N);
        CHECK(sweep_mismatches(&b3) == 0);                                /* 경계 ±16 + 무작위 20만 */
    }

    /* ---------------- Part C: 폴트 기록, 쓰기 버퍼, UART 드라이버 ---------------- */
    bus = new_bus(stm32_map, R_N);
    CHECK(bus_read(&bus, 0x30000000u, 4, &v) == F_UNMAPPED);
    CHECK(bus.nfault == 1 && bus.bfar_valid && bus.bfar == 0x30000000u && bus.last.precise);
    CHECK(bus_write(&bus, 0x08000000u, 2, 0xFFFFu) == F_RO);
    CHECK(bus.nfault == 2 && bus.bfar == 0x30000000u);                    /* 첫 폴트 주소 유지 */
    CHECK(bus.last.addr == 0x08000000u && bus.last.why == F_RO && bus.last.write && bus.last.size == 2
          && bus.last.region == R_FLASH);
    fault_clear(&bus);
    CHECK(bus_read(&bus, 0x40020015u, 2, &v) == F_UNALIGNED && bus.bfar_valid && bus.bfar == 0x40020015u);

    bus = new_bus(stm32_map, R_N);
    CHECK(bus_write_posted(&bus, 0x30000000u, 4, 1u) == BUS_OK && bus.nfault == 0);   /* 아직 안 터짐 */
    CHECK(bus_read(&bus, 0x20000000u, 4, &v) == BUS_OK);                              /* 다음 접근에서 */
    CHECK(bus.nfault == 1 && !bus.last.precise && !bus.bfar_valid && bus.last.addr == 0x30000000u);
    CHECK(bus_write_posted(&bus, 0x20000100u, 4, 0x5A5A5A5Au) == BUS_OK);
    CHECK(bus_read(&bus, 0x20000100u, 4, &v) == BUS_OK && v == 0x5A5A5A5Au);          /* 순서 유지 */
    bus_write_posted(&bus, 0x20000104u, 4, 1u);
    bus_write_posted(&bus, 0x20000108u, 4, 2u);                                       /* 앞 쓰기가 밀려 나감 */
    CHECK(sram_mem[0x104] == 1u && sram_mem[0x108] == 0u && bus.wb_full);

    memset(&usart2, 0, sizeof usart2);
    bus = new_bus(stm32_map, R_N);
    CHECK(uart_putc(&bus, 0x40004400u, 'O', 10) == 0 && uart_putc(&bus, 0x40004400u, 'K', 10) == 0);
    CHECK(strcmp(usart2.tx, "OK") == 0 && usart2.lost == 0);
    CHECK(uart_putc(&bus, 0x40004400u, '!', 2) == -2);                    /* 아직 전송 중 → 타임아웃 */
    CHECK(uart_putc(&bus, 0x40004400u, '!', 10) == 0 && strcmp(usart2.tx, "OK!") == 0);
    CHECK(uart_putc(&bus, 0x40004400u, 'x', 10) == 0);
    bus_write(&bus, 0x40004400u + UART_DR, 4, 'y');                       /* 폴링 없이 바로 또 쓰기 */
    CHECK(usart2.lost == 1 && strcmp(usart2.tx, "OK!x") == 0);

    stm32_map[R_USART2].clk_on = false;                                   /* RCC 클럭 켜는 걸 잊었다 */
    uint32_t nf = bus.nfault;
    int ntx = usart2.ntx;
    CHECK(uart_putc(&bus, 0x40004400u, 'Z', 100) == -2 && bus.nfault == nf && usart2.ntx == ntx);  /* 조용함 */
    stm32_map[R_USART2].gate = GATE_FAULT;
    CHECK(uart_putc(&bus, 0x40004400u, 'Z', 100) == -1);
    CHECK(bus.last.why == F_GATED && bus.last.addr == 0x40004400u && bus.bfar == 0x40004400u);
    stm32_map[R_USART2].gate = GATE_RAZWI; stm32_map[R_USART2].clk_on = true;

    CHECK(bus_write(&bus, 0x40020000u + GPIO_BSRR + 2, 2, 1u << 5) == BUS_OK && gpioa.odr == 0);  /* BSRR 상위 하프워드 = reset */
    CHECK(bus_write(&bus, 0x40020000u + GPIO_ODR + 1, 1, 0x80u) == BUS_OK && gpioa.odr == 0x8000u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
