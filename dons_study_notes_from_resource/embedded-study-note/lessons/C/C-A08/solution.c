/* C-A08 컴파일 타임 계약: _Static_assert 와 BUILD_BUG_ON
 *
 * 가상의 센서 노드 "SNX": 호스트와 SPI 로 12바이트 헤더 패킷을 주고받고, SPI RX 는 DMA 디스크립터 링으로 받는다.
 * 데이터시트/프로토콜 문서의 "가정" (오프셋, 크기, 비트 폭, 정렬, enum 범위, 표 길이) 을 빌드 단계의 계약으로 만든다.
 *
 * 테스트는 두 종류다.
 *   1) 이 파일의 런타임 하네스 — 계약 도구(매크로)의 값과 드라이버 함수를 CHECK 한다.     → "N/M passed"
 *   2) mutants.sh — 이 파일의 구조체/enum/설정을 일부러 망가뜨린 사본(돌연변이)을 만들어
 *      "static assertion 으로 빌드가 실패하는가" 를 본다.                                   → "K/13 killed"
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 *   ./mutants.sh solution.c
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))   /* 포인터를 막는 안전판은 C-M11 의 IS_ARRAY */

/* ======================= Part A — 계약 도구 (모두 정수 상수식이어야 한다) ======================= */

/* A1. 문장 자리에서 쓰는 BUILD_BUG_ON(cond): cond 가 참이면 빌드 실패. cond 가 상수식이 아니면 역시 빌드 실패. */
#define BUILD_BUG_ON(cond) \
    do { _Static_assert(!(cond), "BUILD_BUG_ON(" #cond ")"); } while (0)

/* A2. 식 자리에서 쓰는 BUILD_BUG_ON_ZERO(cond): cond 가 거짓이면 값 0 (size_t), 참이면 빌드 실패. */
#define BUILD_BUG_ON_ZERO(cond) \
    (0 * sizeof(struct { _Static_assert(!(cond), "BUILD_BUG_ON_ZERO(" #cond ")"); char c_; }))

/* A3. x 가 2의 거듭제곱인가 (0 은 아니다). 음수·64비트 값도 UB 없이. 인자는 여러 번 평가된다. */
#define IS_POW2(x) \
    ((uintmax_t)(x) != 0u && ((uintmax_t)(x) & ((uintmax_t)(x) - 1u)) == 0u)

/* A4. 32비트 마스크 m 의 가장 낮은 1 비트 (0x38 → 0x08). m == 0 이면 0. 시프트 대신 곱셈/나눗셈에 쓴다. */
#define LOWBIT(m) \
    ((uint32_t)((uint32_t)(m) & (0u - (uint32_t)(m))))

/* A5. 필드가 담을 수 있는 최댓값 (0x38 → 7, 0xFFFF0000 → 0xFFFF). m == 0 이면 0 (0으로 나누지 않게). */
#define FIELD_MAX(m) \
    ((uint32_t)(m) / (LOWBIT(m) | ((uint32_t)(m) == 0u)))

/* A6. 마스크가 0 이 아니고 1 들이 한 덩어리인가 (0x38 예, 0x28 아니오, 0xFFFFFFFF 예). */
#define MASK_IS_CONTIGUOUS(m) \
    ((uint32_t)(m) != 0u && (FIELD_MAX(m) & (FIELD_MAX(m) + 1u)) == 0u)

/* A7. 값 v 가 필드 m 에 들어가는가. 음수나 2^32 이상이면 거짓 (uint32_t 로 잘라서 비교하면 안 된다). */
#define FIELD_FITS(m, v) \
    ((uintmax_t)(v) <= FIELD_MAX(m))

/* A8. 상수 전용 FIELD_PREP: 마스크가 연속이 아니거나 값이 안 들어가면 빌드 실패. 결과는 uint32_t 상수식. */
#define FIELD_PREP_CONST(m, v)                                   \
    ((uint32_t)(BUILD_BUG_ON_ZERO(!MASK_IS_CONTIGUOUS(m))        \
              + BUILD_BUG_ON_ZERO(!FIELD_FITS(m, v))             \
              + (uint32_t)(v) * LOWBIT(m)))

/* 런타임 값용 (주어진 코드): 호출자가 FIELD_FITS 로 먼저 검사한다 */
static inline uint32_t field_prep(uint32_t m, uint32_t v) { return (v * LOWBIT(m)) & m; }

/* ======================= Part B — 레이아웃 도구 ======================= */

/* B1. 멤버 하나의 크기 (객체 없이). sizeof 의 피연산자는 평가되지 않는다. */
#define MEMBER_SIZE(T, m) \
    sizeof(((T *)0)->m)

/* B2. 멤버 b 가 a 바로 뒤에 붙어 있는가 (사이 패딩 없음). */
#define FIELD_FOLLOWS(T, a, b) \
    (offsetof(T, b) == offsetof(T, a) + MEMBER_SIZE(T, a))

/* B3. 멤버 m 이 구조체의 마지막 바이트에서 끝나는가 (꼬리 패딩 없음). */
#define ENDS_STRUCT(T, m) \
    (offsetof(T, m) + MEMBER_SIZE(T, m) == sizeof(T))

/* B4. 식 e 의 타입이 정확히 type 인가 (_Generic: e 는 평가되지 않음, 한정자는 벗겨짐, 승격은 없음). */
#define IS_TYPE(e, type) \
    _Generic((e), type: 1, default: 0)

/* B5. 스펙 표 한 줄 = 계약 한 줄: T.m 은 offset off 에 있는 type 이다. */
#define PIN_FIELD(T, m, off, type)                                                   \
    _Static_assert(offsetof(T, m) == (off) && IS_TYPE(((T *)0)->m, type),           \
                   #T "." #m " must be " #type " at offset " #off)

/* ---------------- 와이어 프로토콜 (주어진 정의 — mutants.sh 가 이 줄들을 망가뜨린다) ---------------- */
#define PKT_SYNC         0xA5u
#define PKT_MAX_PAYLOAD  256u
#define PKT_ST_ERR       0x07u            /* status[2:0] = err_t      */
#define PKT_ST_ACK       0x08u            /* status[3]                */
#define PKT_ST_RSVD      0xF0u            /* status[7:4] = 0 이어야 함 */

typedef enum {
    PKT_PING = 1,
    PKT_READ,
    PKT_WRITE,
    PKT_STREAM,
    PKT_TYPE_LAST = PKT_STREAM
} pkt_type_t;

typedef enum {
    ERR_OK = 0,
    ERR_TIMEOUT,
    ERR_CRC,
    ERR_LEN,
    ERR_BUSY,
    ERR_PARAM,
    ERR_HW,
    ERR_COUNT                             /* 항목 수 — 항상 마지막 */
} err_t;

static const char *const err_str[] = {    /* 크기를 [ERR_COUNT] 로 적지 않는다 (5. 풀이) */
    [ERR_OK]      = "OK",
    [ERR_TIMEOUT] = "TIMEOUT",
    [ERR_CRC]     = "CRC",
    [ERR_LEN]     = "LEN",
    [ERR_BUSY]    = "BUSY",
    [ERR_PARAM]   = "PARAM",
    [ERR_HW]      = "HW",
};

typedef struct {
    uint8_t  sync;                        /* 0x00  PKT_SYNC                          */
    uint8_t  type;                        /* 0x01  pkt_type_t 값 (enum 타입은 금지)   */
    uint16_t len;                         /* 0x02  payload 바이트 수, LE              */
    uint8_t  status;                      /* 0x04  [2:0] err_t, [3] ACK, [7:4] 0      */
    uint8_t  seq;                         /* 0x05                                    */
    uint16_t rsvd;                        /* 0x06  0                                 */
    uint32_t crc;                         /* 0x08  CRC-32, LE                        */
} pkt_hdr_t;                              /* 12 바이트, 패딩 없음                     */

/* ---------------- SNX-SPI 레지스터 블록과 DMA 디스크립터 (주어진 정의) ---------------- */
typedef struct {
    volatile uint32_t CR1;                /* 0x000 */
    volatile uint32_t CR2;                /* 0x004 */
    volatile uint32_t SR;                 /* 0x008 */
    volatile uint32_t DR;                 /* 0x00C */
    uint32_t          RESERVED0[4];       /* 0x010–0x01F */
    volatile uint32_t DMACR;              /* 0x020 */
    volatile uint32_t DESCADDR;           /* 0x024  첫 디스크립터의 버스 주소 (16B 정렬) */
    uint32_t          RESERVED1[(0x3FCu - 0x028u) / 4u];   /* 0x028–0x3FB */
    volatile uint32_t IDR;                /* 0x3FC  읽기 전용 ID */
} SNX_SPI_TypeDef;                        /* 주소 창 1 KB */

#define SPI_CR1_CPHA     (1u << 0)
#define SPI_CR1_CPOL     (1u << 1)
#define SPI_CR1_MSTR     (1u << 2)
#define SPI_CR1_BR       (0x7u << 3)      /* [5:3] 분주 = 2^(BR+1) */
#define SPI_CR1_SPE      (1u << 6)
#define SPI_DMACR_RXEN   (1u << 0)
#define SPI_DMACR_TXEN   (1u << 1)
#define SPI_DMACR_BURST  (0xFu << 4)      /* [7:4] 버스트 길이(워드) */

#define DESC_LEN         0x0000FFFFu      /* ctrl[15:0] 전송 바이트 수 */
#define DESC_IRQ         (1u << 16)       /* 완료 시 인터럽트          */
#define DESC_OWN         (1u << 31)       /* 1 = DMA 소유              */
#define DESC_ALIGN       16u              /* 디스크립터 주소 정렬 (DESCADDR[3:0] 은 0) */

typedef struct dma_desc {
    _Alignas(DESC_ALIGN) uint32_t ctrl;   /* +0x0 */
    uint32_t src;                         /* +0x4  버스 주소 */
    uint32_t dst;                         /* +0x8  버스 주소 */
    uint32_t next;                        /* +0xC  다음 디스크립터의 버스 주소 (CPU 포인터 아님) */
} dma_desc_t;

/* 보드/앱 설정 — 사람이 바꾸는 숫자들 */
#define SPI_BR_DIV       5u               /* f_PCLK / 64 */
#define SPI_DMA_BURST    4u
#define RING_N           8u
#define RX_BUF_SIZE      512u
#define SPI_DR_BUS       0x4001300Cu      /* SPI DR 의 버스 주소 (DMA src) */

#define SPI_CR1_INIT   (FIELD_PREP_CONST(SPI_CR1_BR, SPI_BR_DIV) | SPI_CR1_MSTR | SPI_CR1_SPE)
#define SPI_DMACR_INIT (FIELD_PREP_CONST(SPI_DMACR_BURST, SPI_DMA_BURST) | SPI_DMACR_RXEN)
static const uint32_t k_cr1_init   = SPI_CR1_INIT;     /* 정적 초기화 = 상수식이어야 함 */
static const uint32_t k_dmacr_init = SPI_DMACR_INIT;

/* ======================= Part B — 계약: 와이어 프로토콜 · enum 범위 · 표 길이 ======================= */
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "pkt_hdr_t is memcpy'd from LE wire bytes: little-endian target required (else serialize, C-M05)"
#endif
_Static_assert(CHAR_BIT == 8, "byte = 8 bits");
_Static_assert(sizeof(pkt_hdr_t) == 12, "pkt_hdr_t must be 12 bytes (wire format)");
PIN_FIELD(pkt_hdr_t, sync,   0x00, uint8_t);
PIN_FIELD(pkt_hdr_t, type,   0x01, uint8_t);
PIN_FIELD(pkt_hdr_t, len,    0x02, uint16_t);
PIN_FIELD(pkt_hdr_t, status, 0x04, uint8_t);
PIN_FIELD(pkt_hdr_t, seq,    0x05, uint8_t);
PIN_FIELD(pkt_hdr_t, rsvd,   0x06, uint16_t);
PIN_FIELD(pkt_hdr_t, crc,    0x08, uint32_t);
_Static_assert(ARRAY_SIZE(err_str) == ERR_COUNT, "err_str[] out of sync with err_t");
_Static_assert(ERR_COUNT <= FIELD_MAX(PKT_ST_ERR) + 1u, "err_t no longer fits status[2:0]");
_Static_assert((PKT_ST_ERR | PKT_ST_ACK | PKT_ST_RSVD) == 0xFFu
               && PKT_ST_ERR + PKT_ST_ACK + PKT_ST_RSVD == 0xFFu, "status bit fields must tile the byte");
_Static_assert(PKT_TYPE_LAST <= UINT8_MAX, "pkt_type_t must fit the uint8_t type field");
_Static_assert(PKT_MAX_PAYLOAD <= UINT16_MAX, "payload length must fit uint16_t len");

/* ======================= Part C — 계약: 레지스터 블록 · 디스크립터 · 설정 ↔ 하드웨어 ======================= */
_Static_assert(sizeof(SNX_SPI_TypeDef) == 0x400, "SNX_SPI block must span 1 KB");
PIN_FIELD(SNX_SPI_TypeDef, CR1,      0x000, uint32_t);
PIN_FIELD(SNX_SPI_TypeDef, DR,       0x00C, uint32_t);
PIN_FIELD(SNX_SPI_TypeDef, DMACR,    0x020, uint32_t);
PIN_FIELD(SNX_SPI_TypeDef, DESCADDR, 0x024, uint32_t);
PIN_FIELD(SNX_SPI_TypeDef, IDR,      0x3FC, uint32_t);
_Static_assert(MASK_IS_CONTIGUOUS(SPI_CR1_BR) && MASK_IS_CONTIGUOUS(SPI_DMACR_BURST)
               && MASK_IS_CONTIGUOUS(DESC_LEN), "multi-bit field masks must be contiguous");
_Static_assert((SPI_CR1_CPHA | SPI_CR1_CPOL | SPI_CR1_MSTR | SPI_CR1_BR | SPI_CR1_SPE)
            == (SPI_CR1_CPHA + SPI_CR1_CPOL + SPI_CR1_MSTR + SPI_CR1_BR + SPI_CR1_SPE),
               "SPI CR1 field masks overlap");          /* 서로소 ⇔ OR == 합 (XOR 는 3개 이상에서 틀림) */
_Static_assert(sizeof(dma_desc_t) == 16, "dma_desc_t must be 16 bytes (hardware layout)");
_Static_assert(_Alignof(dma_desc_t) >= DESC_ALIGN, "dma_desc_t must be 16-byte aligned");
_Static_assert(FIELD_FOLLOWS(dma_desc_t, ctrl, src) && FIELD_FOLLOWS(dma_desc_t, src, dst)
               && FIELD_FOLLOWS(dma_desc_t, dst, next) && ENDS_STRUCT(dma_desc_t, next),
               "dma_desc_t must have no padding");
_Static_assert(IS_TYPE(((dma_desc_t *)0)->next, uint32_t), "dma_desc_t.next is a 32-bit bus address, not a pointer");
_Static_assert(IS_POW2(RING_N), "RING_N must be a power of two (index wrap uses & (RING_N - 1))");
_Static_assert(RX_BUF_SIZE > 0u && FIELD_FITS(DESC_LEN, RX_BUF_SIZE), "RX_BUF_SIZE must fit descriptor LEN[15:0]");
_Static_assert(RX_BUF_SIZE % 4u == 0u, "RX_BUF_SIZE must be a multiple of 4 (word DMA)");
_Static_assert(sizeof(pkt_hdr_t) + PKT_MAX_PAYLOAD <= RX_BUF_SIZE, "a max-size packet must fit one RX buffer");

/* ======================= Part B — 런타임: 신뢰할 수 없는 바이트 검증 ======================= */
enum { PKT_OK = 0, PKT_E_SHORT = -1, PKT_E_SYNC = -2, PKT_E_TYPE = -3,
       PKT_E_STATUS = -4, PKT_E_RSVD = -5, PKT_E_LEN = -6 };

/* B6. raw[0..n) 를 헤더로 해석하고 검증한다. 성공 시 *out 채우고 PKT_OK.
 *     순서: 길이 → sync → type 범위 → status (err 범위, 예약 비트) → rsvd → len (≤ PKT_MAX_PAYLOAD, ≤ 남은 바이트) */
static int pkt_parse(const uint8_t *raw, size_t n, pkt_hdr_t *out) {
    if (n < sizeof *out) return PKT_E_SHORT;
    memcpy(out, raw, sizeof *out);                /* 레이아웃·LE 계약이 이 한 줄을 정당화한다 */
    if (out->sync != PKT_SYNC) return PKT_E_SYNC;
    if (out->type < PKT_PING || out->type > PKT_TYPE_LAST) return PKT_E_TYPE;
    if ((out->status & PKT_ST_ERR) >= ERR_COUNT || (out->status & PKT_ST_RSVD) != 0u) return PKT_E_STATUS;
    if (out->rsvd != 0u) return PKT_E_RSVD;
    if (out->len > PKT_MAX_PAYLOAD || out->len > n - sizeof *out) return PKT_E_LEN;
    return PKT_OK;
}

static const char *err_name(unsigned e) {          /* 주어진 코드 */
    return (e < ARRAY_SIZE(err_str) && err_str[e]) ? err_str[e] : "?";
}

/* ======================= Part C — 런타임: 디스크립터 링과 SPI DMA 시작 ======================= */

/* C1. ring[0..n) 을 원형 링으로 채운다. 실패 시 -1 (아무것도 쓰지 않음):
 *     n 이 2의 거듭제곱이 아님 / buf_len 이 0 이거나 LEN 필드에 안 들어감 / ring_bus 가 DESC_ALIGN 정렬이 아님.
 *     ctrl = LEN(buf_len) | IRQ | OWN, src = SPI_DR_BUS, dst = buf_bus + i*buf_len,
 *     next = ring_bus + ((i+1) & (n-1)) * 16  (마지막은 첫 번째로) */
static int dma_ring_init(dma_desc_t *ring, uint32_t n, uint32_t ring_bus, uint32_t buf_bus, uint32_t buf_len) {
    if (!IS_POW2(n) || buf_len == 0u || !FIELD_FITS(DESC_LEN, buf_len) || ring_bus % DESC_ALIGN != 0u)
        return -1;
    for (uint32_t i = 0; i < n; i++) {
        ring[i].ctrl = field_prep(DESC_LEN, buf_len) | DESC_IRQ | DESC_OWN;
        ring[i].src  = SPI_DR_BUS;
        ring[i].dst  = buf_bus + i * buf_len;
        ring[i].next = ring_bus + ((i + 1u) & (n - 1u)) * (uint32_t)sizeof(dma_desc_t);
    }
    return 0;
}

/* C2. SPE 를 끈 채 설정 → DESCADDR, DMACR → 마지막에 CR1 (SPE 켜기). 값은 컴파일 타임 상수. */
static void spi_dma_start(SNX_SPI_TypeDef *spi, uint32_t ring_bus) {
    spi->CR1      = 0u;
    spi->DESCADDR = ring_bus;
    spi->DMACR    = k_dmacr_init;
    spi->CR1      = k_cr1_init;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 상수식이어야만 컴파일되는 자리들 (스텁도 상수식이라 starter 도 컴파일된다) */
enum { ICE_PREP = FIELD_PREP_CONST(0x38u, 5u), ICE_MAX = FIELD_MAX(0xF0u) };
typedef char ice_arr_t[FIELD_MAX(0x38u) + 1u];      /* 파일 범위 배열 크기: VLA 불가 */

struct pad_demo { uint8_t a; uint32_t b; uint16_t c; };    /* a 뒤 3바이트, c 뒤 2바이트 패딩 */

static const uint8_t k_golden_hdr[12] = {                  /* PING, len 4, ACK, seq 7, crc 0xDEADBEEF */
    0xA5, 0x01, 0x04, 0x00, 0x08, 0x07, 0x00, 0x00, 0xEF, 0xBE, 0xAD, 0xDE };

static int parse_with(uint8_t idx, uint8_t val, size_t n) {   /* 골든 + 한 바이트 변조 + payload 4바이트 */
    uint8_t buf[12 + 4] = {0};
    pkt_hdr_t h;
    memcpy(buf, k_golden_hdr, 12);
    buf[idx] = val;
    return pkt_parse(buf, n, &h);
}

int main(void) {
    /* ---- Part A ---- */
    BUILD_BUG_ON(sizeof(pkt_hdr_t) != 12);
    CHECK(BUILD_BUG_ON_ZERO(0) == 0 && sizeof(BUILD_BUG_ON_ZERO(0)) == sizeof(size_t));
    CHECK(IS_POW2(1) && IS_POW2(2) && IS_POW2(64u) && IS_POW2(0x80000000u) && IS_POW2(1ull << 40));
    CHECK(!IS_POW2(0) && !IS_POW2(3) && !IS_POW2(96) && !IS_POW2(0xFFFFFFFFu) && !IS_POW2(-8));
    CHECK(LOWBIT(0x38u) == 0x08u && LOWBIT(0xFFFF0000u) == 0x10000u && LOWBIT(0x80000000u) == 0x80000000u
          && LOWBIT(0u) == 0u);
    CHECK(FIELD_MAX(0x38u) == 7u && FIELD_MAX(0xFFFF0000u) == 0xFFFFu && FIELD_MAX(0x80000000u) == 1u);
    CHECK(FIELD_MAX(0xFFFFFFFFu) == 0xFFFFFFFFu && FIELD_MAX(0u) == 0u);
    CHECK(MASK_IS_CONTIGUOUS(0x38u) && MASK_IS_CONTIGUOUS(0xFFFFFFFFu) && MASK_IS_CONTIGUOUS(0x80000000u)
          && MASK_IS_CONTIGUOUS(0xFFFF0000u) && MASK_IS_CONTIGUOUS(0x7FFFFFFFu));
    CHECK(!MASK_IS_CONTIGUOUS(0u) && !MASK_IS_CONTIGUOUS(0x28u) && !MASK_IS_CONTIGUOUS(0xF0F0u)
          && !MASK_IS_CONTIGUOUS(0x80000001u));
    CHECK(FIELD_FITS(0x38u, 0) && FIELD_FITS(0x38u, 7u) && !FIELD_FITS(0x38u, 8u) && !FIELD_FITS(0x38u, 56u));
    CHECK(FIELD_FITS(0xFFFFFFFFu, 0xFFFFFFFFu) && !FIELD_FITS(0xFFFFFFFFu, 0x100000000ull) && !FIELD_FITS(0x38u, -1));
    CHECK(FIELD_PREP_CONST(0x38u, 5u) == 0x28u && FIELD_PREP_CONST(0xFFFF0000u, 0xABCDu) == 0xABCD0000u);
    CHECK(FIELD_PREP_CONST(0x80000000u, 1u) == 0x80000000u && FIELD_PREP_CONST(0xFFu, 0xFFu) == 0xFFu);
    CHECK(ICE_PREP == 0x28 && ICE_MAX == 15 && sizeof(ice_arr_t) == 8u);
    {
        int single_bits_ok = 1;                               /* 단일 비트 32개: 최댓값 1, 넣으면 그 비트 */
        for (uint32_t m = 1u; m != 0u; m <<= 1)
            single_bits_ok &= FIELD_MAX(m) == 1u && field_prep(m, 1u) == m && MASK_IS_CONTIGUOUS(m);
        CHECK(single_bits_ok);
    }

    /* ---- Part B ---- */
    CHECK(MEMBER_SIZE(pkt_hdr_t, len) == 2u && MEMBER_SIZE(pkt_hdr_t, crc) == 4u && MEMBER_SIZE(pkt_hdr_t, seq) == 1u);
    CHECK(MEMBER_SIZE(SNX_SPI_TypeDef, RESERVED0) == 16u && MEMBER_SIZE(SNX_SPI_TypeDef, RESERVED1) == 980u);
    CHECK(!FIELD_FOLLOWS(struct pad_demo, a, b) && FIELD_FOLLOWS(struct pad_demo, b, c)
          && !ENDS_STRUCT(struct pad_demo, c));
    CHECK(FIELD_FOLLOWS(pkt_hdr_t, sync, type) && FIELD_FOLLOWS(pkt_hdr_t, rsvd, crc) && ENDS_STRUCT(pkt_hdr_t, crc));
    {
        uint8_t a8 = 1, b8 = 2;
        (void)a8; (void)b8;                                   /* _Generic 은 평가하지 않는다 */
        CHECK(IS_TYPE((uint16_t)1, uint16_t) && !IS_TYPE((int16_t)1, uint16_t));
        CHECK(IS_TYPE('a', int) && !IS_TYPE('a', char));          /* C 에서 문자 상수는 int */
        CHECK(IS_TYPE(a8 + b8, int) && IS_TYPE(a8, uint8_t));     /* 정수 승격 (C-B02) */
        CHECK(IS_TYPE(((SNX_SPI_TypeDef *)0)->CR1, uint32_t));   /* volatile 은 벗겨진다 */
    }
    CHECK(err_str[ERR_HW] != NULL && strcmp(err_name(ERR_CRC), "CRC") == 0 && strcmp(err_name(99), "?") == 0);
    {
        int err_str_holes = 0;                                /* 표에 구멍(NULL)이 없어야 한다 */
        for (unsigned e = 0; e < ERR_COUNT; e++) err_str_holes += (err_str[e] == NULL);
        CHECK(err_str_holes == 0);
    }
    {
        pkt_hdr_t h;
        uint8_t buf[16] = {0};
        memcpy(buf, k_golden_hdr, 12);
        CHECK(pkt_parse(buf, 16, &h) == PKT_OK && h.type == PKT_PING && h.len == 4u
              && h.status == PKT_ST_ACK && h.seq == 7u && h.crc == 0xDEADBEEFu);
    }
    CHECK(parse_with(0, 0xA5, 11) == PKT_E_SHORT);
    CHECK(parse_with(0, 0x5A, 16) == PKT_E_SYNC);
    CHECK(parse_with(1, 0x00, 16) == PKT_E_TYPE && parse_with(1, 0x05, 16) == PKT_E_TYPE
          && parse_with(1, PKT_TYPE_LAST, 16) == PKT_OK);
    CHECK(parse_with(4, 0x07, 16) == PKT_E_STATUS && parse_with(4, 0x0E, 16) == PKT_OK
          && parse_with(4, 0x18, 16) == PKT_E_STATUS);
    CHECK(parse_with(7, 0x01, 16) == PKT_E_RSVD);
    CHECK(parse_with(2, 0x05, 16) == PKT_E_LEN && parse_with(3, 0x01, 16) == PKT_E_LEN
          && parse_with(2, 0x00, 12) == PKT_OK);

    /* ---- Part C ---- */
    CHECK(k_cr1_init == 0x6Cu && k_dmacr_init == 0x41u);
    {
        dma_desc_t ring[4];
        uint8_t bytes[16];
        memset(ring, 0xCC, sizeof ring);
        CHECK(dma_ring_init(ring, 4u, 0x20000100u, 0x20001000u, RX_BUF_SIZE) == 0);
        CHECK(ring[0].ctrl == 0x80010200u && ring[3].ctrl == 0x80010200u && ring[2].src == SPI_DR_BUS);
        CHECK(ring[0].dst == 0x20001000u && ring[1].dst == 0x20001200u && ring[3].dst == 0x20001600u);
        CHECK(ring[0].next == 0x20000110u && ring[2].next == 0x20000130u && ring[3].next == 0x20000100u);
        memcpy(bytes, &ring[1], 16);                          /* 하드웨어가 메모리에서 보는 그대로 */
        CHECK(bytes[0] == 0x00 && bytes[1] == 0x02 && bytes[2] == 0x01 && bytes[3] == 0x80
              && bytes[8] == 0x00 && bytes[9] == 0x12 && bytes[12] == 0x20 && bytes[13] == 0x01);
        memset(ring, 0xCC, sizeof ring);
        CHECK(dma_ring_init(ring, 3u, 0x20000100u, 0x20001000u, 512u) == -1);          /* 2^k 아님 */
        CHECK(dma_ring_init(ring, 4u, 0x20000100u, 0x20001000u, 0u) == -1
              && dma_ring_init(ring, 4u, 0x20000100u, 0x20001000u, 0x10000u) == -1);    /* LEN 범위 */
        CHECK(dma_ring_init(ring, 4u, 0x20000108u, 0x20001000u, 512u) == -1);          /* 정렬 */
        CHECK(ring[0].ctrl == 0xCCCCCCCCu);                                             /* 실패 시 안 씀 */
        CHECK(dma_ring_init(ring, 1u, 0x20000100u, 0x20001000u, 0xFFFFu) == 0
              && ring[0].next == 0x20000100u && ring[0].ctrl == 0x8001FFFFu);
    }
    {
        static SNX_SPI_TypeDef fake;                          /* RAM 에 둔 가짜 레지스터 블록 (0 초기화) */
        fake.CR1 = 0xFFFFFFFFu;
        spi_dma_start(&fake, 0x20000100u);
        CHECK(fake.CR1 == 0x6Cu && fake.DMACR == 0x41u && fake.DESCADDR == 0x20000100u);
        CHECK((fake.CR1 & SPI_CR1_BR) == FIELD_PREP_CONST(SPI_CR1_BR, SPI_BR_DIV) && (fake.CR1 & SPI_CR1_SPE));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
