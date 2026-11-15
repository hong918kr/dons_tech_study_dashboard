/* COMM-B06 I2C 비트 수준: START/STOP, 주소+R/W, ACK/NACK
 *
 * 모델: SDA/SCL 각 선 = 모든 노드 드라이버의 AND (오픈드레인 + 풀업: 놓으면 1, 당기면 0).
 *       마스터가 선 하나를 바꿀 때마다 bus_tick() 한 번 = 이산 시간 1스텝.
 *       슬레이브는 매 틱 선 레벨을 보고(엣지 검출) 자기 SDA 드라이버만 바꾼다.
 * Part A: wired-AND 해석 + 비트뱅 마스터 (START, STOP, 비트/바이트 쓰기·읽기, 9번째 클럭 ACK)
 * Part B: 슬레이브 상태 머신 (START/STOP 검출, 주소 비교, ACK/NACK, 송신) + 규칙 위반 주입
 * Part C: 드라이버 API i2c_write / i2c_read / i2c_scan + 에러 코드 (주소 NACK, 데이터 NACK, 버스 busy)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 버스 모델 (주어짐) ---------------- */
enum { MAX_NODES = 4, TRACE_CAP = 1024 };

typedef struct Slave Slave;

typedef struct {
    uint8_t  sda_drv[MAX_NODES];  /* 노드별 드라이버: 1 = 놓음(풀업이 올림), 0 = LOW 로 당김 */
    uint8_t  scl_drv[MAX_NODES];
    int      nnodes;              /* node 0 = 마스터 */
    uint8_t  sda, scl;            /* 해석된(resolved) 선 레벨 */
    Slave   *slaves[MAX_NODES];
    int      nslaves;
    uint32_t ticks;               /* 지난 틱 수 */
    uint32_t sda_moves_scl_high;  /* SCL=1 인 동안 SDA 가 바뀐 횟수 (정상이면 = START + STOP 수) */
    char     tr_scl[TRACE_CAP + 1], tr_sda[TRACE_CAP + 1]; /* 틱별 최종 레벨 '0'/'1' */
    uint32_t tr_len;
} Bus;

typedef enum { S_IDLE, S_ADDR, S_ADDR_ACK, S_RX, S_RX_ACK, S_TX, S_TX_ACK } SState;

struct Slave {
    int      node;                /* 버스 노드 번호 (자기 SDA 드라이버 = b->sda_drv[node]) */
    uint8_t  addr;                /* 7비트 주소 */
    SState   st;
    uint8_t  prev_scl, prev_sda;  /* 지난 틱에 본 레벨 */
    uint8_t  sh;                  /* 시프트 레지스터 */
    int      nbits;               /* 이번 바이트에서 본 SCL 상승 엣지 수 */
    bool     rw;                  /* 1 = 마스터가 읽기 */
    bool     master_ack;          /* 송신 중 9번째 클럭에 마스터가 ACK(0) 했나 */
    uint8_t  rx[16];
    size_t   rx_len, rx_cap;      /* rx_cap 개 넘게 오면 그 바이트를 NACK */
    uint8_t  tx[16];
    size_t   tx_len, tx_pos;      /* 마스터가 읽어 갈 바이트, 다 쓰면 0xFF */
    uint32_t starts, stops, addr_matches;
};

/* ---------------- Part A: wired-AND + 비트뱅 마스터 ---------------- */

/* 모든 노드 드라이버의 AND. 아무도 안 당기면 풀업 때문에 1. */
static void bus_resolve(Bus *b) {
    b->sda = b->sda_drv[0];   /* TODO: 모든 노드(0..nnodes-1)의 AND. 지금은 마스터만 본다 */
    b->scl = b->scl_drv[0];
}

static void slave_on_tick(Slave *s, Bus *b);

/* 1 틱: (마스터 변경 반영) → 슬레이브들이 같은 스냅샷을 보고 반응 → (슬레이브 변경 반영) → 기록 */
static void bus_tick(Bus *b) {
    uint8_t psda = b->sda, pscl = b->scl;
    bus_resolve(b);
    for (int i = 0; i < b->nslaves; i++) slave_on_tick(b->slaves[i], b);
    bus_resolve(b);
    b->ticks++;
    if (pscl && b->scl && psda != b->sda) b->sda_moves_scl_high++;
    if (b->tr_len < TRACE_CAP) {
        b->tr_scl[b->tr_len] = (char)('0' + b->scl);
        b->tr_sda[b->tr_len] = (char)('0' + b->sda);
        b->tr_len++;
        b->tr_scl[b->tr_len] = b->tr_sda[b->tr_len] = '\0';
    }
}

/* 마스터(node 0)가 선 하나를 바꾼다 = 1 틱 */
static void set_sda(Bus *b, int v) { b->sda_drv[0] = (uint8_t)(v != 0); bus_tick(b); }
static void set_scl(Bus *b, int v) { b->scl_drv[0] = (uint8_t)(v != 0); bus_tick(b); }

/* START: SCL=1 인 동안 SDA 1→0, 그 다음 SCL 0. (SCL=0 에서 부르면 먼저 SDA·SCL 을 올린다 = Sr, COMM-B07) */
static void m_start(Bus *b) {
    (void)b; /* TODO: (SCL=0 이면 먼저 SDA 1, SCL 1) → SDA 0 → SCL 0 */
}

/* STOP: (SCL=0 에서) SDA 0 → SCL 1 → SDA 0→1 */
static void m_stop(Bus *b) {
    (void)b; /* TODO: SDA 0 → SCL 1 → SDA 1 */
}

/* 비트 쓰기: SCL=0 인 동안 SDA 를 정하고 → SCL 1 (슬레이브가 샘플) → SCL 0 */
static void m_write_bit(Bus *b, int bit) {
    (void)b; (void)bit; /* TODO: SDA = bit → SCL 1 → SCL 0 */
}

/* 비트 읽기: SDA 를 놓고 → SCL 1 → SDA 읽기 → SCL 0 */
static int m_read_bit(Bus *b) {
    (void)b;
    return 1; /* TODO: SDA 놓기 → SCL 1 → SDA 읽기 → SCL 0 */
}

/* MSB first 8비트 + 9번째 클럭에 ACK 읽기. 반환: true = ACK(SDA=0) */
static bool m_write_byte(Bus *b, uint8_t byte) {
    (void)b; (void)byte; (void)m_write_bit;
    return false; /* TODO: MSB 부터 8비트 m_write_bit, 9번째 m_read_bit == 0 이면 ACK */
}

/* 8비트 읽기 + 9번째 클럭에 ACK(0) 또는 NACK(1) 쓰기 */
static uint8_t m_read_byte(Bus *b, bool ack) {
    (void)b; (void)ack;
    return 0; /* TODO: 8비트 m_read_bit (MSB first), 9번째에 ack ? 0 : 1 쓰기 */
}

/* ---------------- Part B: 슬레이브 상태 머신 ---------------- */

/* 매 틱 호출. 규칙: SCL=1 동안 SDA 변화 = START(↓)/STOP(↑), SCL↑ 에서 샘플, SCL↓ 에서만 SDA 변경. */
static void slave_on_tick(Slave *s, Bus *b) {
    (void)s; (void)b;
    /* TODO:
     * 1) SCL 이 계속 1 인데 SDA 가 바뀜 → ↓ START (starts++, S_ADDR, nbits=sh=0) / ↑ STOP (stops++, S_IDLE). SDA 놓기.
     * 2) SCL ↑ → S_ADDR/S_RX 면 sh = sh<<1 | sda, nbits++.  S_TX 면 nbits++.  S_TX_ACK 면 master_ack = (sda==0).
     * 3) SCL ↓ → 상태별로 SDA 를 바꾼다:
     *    S_ADDR & nbits==8 : 주소 일치면 ACK(0) 내고 S_ADDR_ACK (rw, addr_matches++), 아니면 S_IDLE
     *    S_RX   & nbits==8 : rx 에 자리 있으면 저장 + ACK, 없으면 놓아 둠(NACK). → S_RX_ACK
     *    S_ADDR_ACK/S_RX_ACK : 읽기면 tx 바이트(없으면 0xFF) MSB 를 내고 S_TX, 아니면 놓고 S_RX
     *    S_TX   : nbits<8 이면 다음 비트, 8 이면 놓고 S_TX_ACK
     *    S_TX_ACK : 마스터 ACK 면 다음 바이트 MSB, NACK 면 놓고 S_IDLE
     * 4) prev_scl/prev_sda 갱신 */
}

/* ---------------- Part C: 드라이버 API ---------------- */
enum { I2C_OK = 0, I2C_ERR_ARG = -1, I2C_ERR_BUS_BUSY = -2, I2C_ERR_NACK_ADDR = -3, I2C_ERR_NACK_DATA = -4 };

/* START, addr7+W, data[0..n). NACK 이면 즉시 STOP 하고 에러. *n_acked = ACK 받은 데이터 바이트 수. */
static int i2c_write(Bus *b, uint8_t addr7, const uint8_t *data, size_t n, size_t *n_acked) {
    (void)b; (void)addr7; (void)data; (void)n;
    if (n_acked) *n_acked = 0;
    return I2C_ERR_BUS_BUSY; /* TODO: 인자 검사 → idle 검사 → START, addr+W, 데이터, (NACK 면 중단) STOP */
}

/* START, addr7+R, n 바이트 읽기 (마지막 바이트만 NACK), STOP. n == 0 은 I2C 로 불가 → ARG */
static int i2c_read(Bus *b, uint8_t addr7, uint8_t *buf, size_t n) {
    (void)b; (void)addr7; (void)buf; (void)n;
    return I2C_ERR_BUS_BUSY; /* TODO: 인자 검사 → idle 검사 → START, addr+R, n 바이트(마지막만 NACK), STOP */
}

/* 0x08..0x77 에 주소만 보내 본다 (START, addr+W, STOP). ACK 한 주소를 found[] 에 cap 개까지 저장.
 * 반환 = ACK 한 주소의 총 개수 (> cap 이면 잘렸다는 뜻), 버스 busy 면 I2C_ERR_BUS_BUSY. */
static int i2c_scan(Bus *b, uint8_t *found, size_t cap) {
    (void)b; (void)found; (void)cap;
    return 0; /* TODO: 0x08..0x77 각각 i2c_write(b, a, NULL, 0, NULL) == I2C_OK 인 주소를 모은다 */
}

/* ---------------- 테스트 하네스 (주어짐) ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void bus_init(Bus *b) {
    memset(b, 0, sizeof *b);
    b->nnodes = 1;
    b->sda_drv[0] = b->scl_drv[0] = 1;
    b->sda = b->scl = 1;
}
static int bus_add_node(Bus *b) {           /* 고장 주입용 노드 (처음엔 놓음) */
    int i = b->nnodes++;
    b->sda_drv[i] = b->scl_drv[i] = 1;
    return i;
}
static void bus_add_slave(Bus *b, Slave *s, uint8_t addr, size_t rx_cap) {
    memset(s, 0, sizeof *s);
    s->node = bus_add_node(b);
    s->addr = addr;
    s->rx_cap = rx_cap < sizeof s->rx ? rx_cap : sizeof s->rx;
    s->prev_scl = b->scl;
    s->prev_sda = b->sda;
    b->slaves[b->nslaves++] = s;
}
static void trace_reset(Bus *b) {           /* 지금 레벨을 0번 칸으로 두고 기록 시작 */
    b->tr_len = 1;
    b->tr_scl[0] = (char)('0' + b->scl); b->tr_sda[0] = (char)('0' + b->sda);
    b->tr_scl[1] = b->tr_sda[1] = '\0';
    b->sda_moves_scl_high = 0;
}
/* 로직 애널라이저처럼 출력: '-' = 1, '_' = 0, 이벤트 줄 S/P = SCL high 중 SDA ↓/↑, ^ = SCL ↑ (샘플) */
static void trace_print(const Bus *b, const char *title) {
    printf("%s (%u ticks)\n  SCL ", title, (unsigned)b->tr_len);
    for (uint32_t i = 0; i < b->tr_len; i++) putchar(b->tr_scl[i] == '1' ? '-' : '_');
    printf("\n  SDA ");
    for (uint32_t i = 0; i < b->tr_len; i++) putchar(b->tr_sda[i] == '1' ? '-' : '_');
    printf("\n      ");
    for (uint32_t i = 0; i < b->tr_len; i++) {
        char c = ' ';
        if (i > 0 && b->tr_scl[i - 1] == '1' && b->tr_scl[i] == '1' && b->tr_sda[i - 1] != b->tr_sda[i])
            c = b->tr_sda[i] == '0' ? 'S' : 'P';
        else if (i > 0 && b->tr_scl[i - 1] == '0' && b->tr_scl[i] == '1') c = '^';
        putchar(c);
    }
    putchar('\n');
}
/* 일부러 틀린 비트뱅: SCL 을 먼저 올리고 SDA 를 바꾼다 (SCL high 중 SDA 변화!) */
static void m_write_bit_bad(Bus *b, int bit) {
    set_scl(b, 1);
    set_sda(b, bit);
    set_scl(b, 0);
}
static bool m_write_byte_bad(Bus *b, uint8_t byte) {
    for (int i = 7; i >= 0; i--) m_write_bit_bad(b, (byte >> i) & 1);
    return m_read_bit(b) == 0;
}
static uint32_t xs_state = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}

/* 주소 0x50 + W 프로브(START, A0, 9번째 클럭, STOP)의 기대 파형. 0번 칸 = idle */
static const char *WAVE_SCL     = "1" "10" "010010010010010010010010" "010" "011";
static const char *WAVE_SDA_ACK = "1" "00" "111000111000000000000000" "001" "001";
static const char *WAVE_SDA_NAK = "1" "00" "111000111000000000000000" "111" "001";

int main(void) {
    Bus b; Slave s1, s2;

    /* ===== Part A: wired-AND ===== */
    bus_init(&b); bus_add_node(&b); bus_add_node(&b);
    int ok = 0;
    for (unsigned m = 0; m < 8; m++) {                      /* 3 노드 × (놓음/당김) 전수 */
        for (int i = 0; i < 3; i++) { b.sda_drv[i] = (uint8_t)(m >> i & 1); b.scl_drv[i] = (uint8_t)(~m >> i & 1); }
        bus_resolve(&b);
        ok += (b.sda == (m == 7)) && (b.scl == (m == 0));
    }
    CHECK(ok == 8);

    /* ===== Part A: 비트뱅 마스터 단독 (슬레이브 없음 = 아무도 ACK 안 함) ===== */
    bus_init(&b);
    trace_reset(&b);
    m_start(&b);
    bool ack = m_write_byte(&b, 0x50 << 1);
    m_stop(&b);
    CHECK(!ack);                                           /* 아무도 안 당김 → SDA=1 = NACK */
    CHECK(strcmp(b.tr_scl, WAVE_SCL) == 0);
    CHECK(strcmp(b.tr_sda, WAVE_SDA_NAK) == 0);
    CHECK(b.sda_moves_scl_high == 2);                      /* START 1 + STOP 1 뿐 */
    CHECK(b.sda == 1 && b.scl == 1);                       /* STOP 뒤 idle */
    trace_print(&b, "probe 0x50+W, no device (NACK)");
    trace_reset(&b);
    m_start(&b);
    uint8_t v = m_read_byte(&b, false);                    /* 아무도 안 내면 풀업 = 0xFF */
    m_stop(&b);
    CHECK(v == 0xFF);
    CHECK(b.tr_len == 1 + 2 + 27 + 3);

    /* ===== Part B: 슬레이브 — 주소 ACK 파형, 주소 불일치 무시 ===== */
    bus_init(&b); bus_add_slave(&b, &s1, 0x50, 4); bus_add_slave(&b, &s2, 0x51, 4);
    trace_reset(&b);
    m_start(&b);
    ack = m_write_byte(&b, 0x50 << 1);
    m_stop(&b);
    CHECK(ack);
    CHECK(strcmp(b.tr_scl, WAVE_SCL) == 0);
    CHECK(strcmp(b.tr_sda, WAVE_SDA_ACK) == 0);            /* 9번째 클럭 SDA=0 은 슬레이브가 당긴 것 */
    CHECK(s1.addr_matches == 1 && s2.addr_matches == 0);   /* 0x51 은 마지막 비트(R/W 앞)가 달라 무시 */
    CHECK(s1.starts == 1 && s1.stops == 1 && s2.starts == 1 && s2.stops == 1);
    CHECK(s1.st == S_IDLE && b.sda_drv[s1.node] == 1);
    trace_print(&b, "probe 0x50+W, device present (ACK)");

    /* ===== Part B: N 바이트 쓰기, 데이터 NACK (rx_cap 초과) ===== */
    s1.rx_len = 0; s1.rx_cap = 2;
    m_start(&b);
    bool a0 = m_write_byte(&b, 0x50 << 1);
    bool d0 = m_write_byte(&b, 0x11), d1 = m_write_byte(&b, 0x22), d2 = m_write_byte(&b, 0x33);
    m_stop(&b);
    CHECK(a0 && d0 && d1 && !d2);                          /* 3번째 데이터 바이트에서 NACK */
    CHECK(s1.rx_len == 2 && s1.rx[0] == 0x11 && s1.rx[1] == 0x22);
    CHECK(s2.rx_len == 0);
    CHECK(b.sda == 1 && b.scl == 1);

    /* ===== Part B: 읽기 — 슬레이브 송신, 마스터 ACK/ACK/NACK ===== */
    s1.tx[0] = 0x12; s1.tx[1] = 0x34; s1.tx[2] = 0x56; s1.tx_len = 3; s1.tx_pos = 0;
    m_start(&b);
    a0 = m_write_byte(&b, 0x50 << 1 | 1);
    uint8_t r0 = m_read_byte(&b, true), r1 = m_read_byte(&b, true), r2 = m_read_byte(&b, false);
    m_stop(&b);
    CHECK(a0 && r0 == 0x12 && r1 == 0x34 && r2 == 0x56);
    CHECK(s1.tx_pos == 3 && s1.st == S_IDLE);

    /* ===== Part B: 256 값 전수 쓰기·읽기 + "SCL high 중 SDA 변화 = START/STOP 뿐" 불변식 ===== */
    trace_reset(&b);
    int bad = 0;
    s1.rx_cap = 16;
    for (int x = 0; x < 256; x++) {
        s1.rx_len = 0;
        m_start(&b);
        bool aa = m_write_byte(&b, 0x50 << 1), dd = m_write_byte(&b, (uint8_t)x);
        m_stop(&b);
        if (!aa || !dd || s1.rx_len != 1 || s1.rx[0] != x) bad++;
        s1.tx[0] = (uint8_t)x; s1.tx_len = 1; s1.tx_pos = 0;
        m_start(&b);
        aa = m_write_byte(&b, 0x50 << 1 | 1);
        uint8_t got = m_read_byte(&b, false);
        m_stop(&b);
        if (!aa || got != x) bad++;
    }
    CHECK(bad == 0);
    CHECK(b.sda_moves_scl_high == 512 * 2);
    CHECK(s1.starts == s1.stops && s2.starts == s1.starts);

    /* ===== Part B: 규칙 위반 주입 — SCL high 중 SDA 를 바꾸는 마스터 ===== */
    uint32_t st0 = s1.starts, sp0 = s1.stops;
    s1.rx_len = 0;
    trace_reset(&b);
    m_start(&b);
    a0 = m_write_byte(&b, 0x50 << 1);
    d0 = m_write_byte_bad(&b, 0xA5);                       /* 1010 0101: 비트가 바뀔 때마다 S 또는 P */
    m_stop(&b);
    CHECK(a0 && !d0);                                      /* 슬레이브가 이미 IDLE → 9번째 클럭 NACK */
    CHECK(s1.rx_len == 0);
    CHECK(s1.starts - st0 == 4 && s1.stops - sp0 == 4);    /* 진짜 1+1, 가짜 START 3 + 가짜 STOP 3 */
    CHECK(b.sda_moves_scl_high == 8);
    trace_print(&b, "bad master: 0xA5 with SDA changed while SCL high");
    s1.rx_len = 0;
    m_start(&b);
    a0 = m_write_byte(&b, 0x50 << 1);
    d0 = m_write_byte_bad(&b, 0xFF);                       /* 비트가 안 바뀌면 버그가 숨는다 */
    m_stop(&b);
    CHECK(a0 && d0 && s1.rx_len == 1 && s1.rx[0] == 0xFF);

    /* ===== Part C: 드라이버 API ===== */
    bus_init(&b); bus_add_slave(&b, &s1, 0x50, 4); bus_add_slave(&b, &s2, 0x68, 16);
    int fault = bus_add_node(&b);
    const uint8_t w3[3] = {0xDE, 0xAD, 0x01};
    size_t na = 99;
    CHECK(i2c_write(&b, 0x50, w3, 3, &na) == I2C_OK && na == 3);
    CHECK(s1.rx_len == 3 && memcmp(s1.rx, w3, 3) == 0 && s2.rx_len == 0);
    CHECK(i2c_write(&b, 0x3C, w3, 3, &na) == I2C_ERR_NACK_ADDR && na == 0);   /* 없는 디바이스 */
    CHECK(b.sda == 1 && b.scl == 1);                                           /* NACK 뒤에도 STOP */
    const uint8_t w4[4] = {1, 2, 3, 4};
    s1.rx_len = 0; s1.rx_cap = 2;
    CHECK(i2c_write(&b, 0x50, w4, 4, &na) == I2C_ERR_NACK_DATA && na == 2);   /* 데이터 NACK */
    CHECK(s1.rx_len == 2 && b.sda == 1 && b.scl == 1);
    CHECK(i2c_write(&b, 0xA0, w3, 1, &na) == I2C_ERR_ARG);                    /* 8비트 주소 실수 */
    CHECK(i2c_write(&b, 0x50, NULL, 2, &na) == I2C_ERR_ARG);
    uint8_t rb[4] = {0};
    s2.tx[0] = 0xC0; s2.tx[1] = 0xFF; s2.tx[2] = 0xEE; s2.tx_len = 3; s2.tx_pos = 0;
    CHECK(i2c_read(&b, 0x68, rb, 3) == I2C_OK && rb[0] == 0xC0 && rb[1] == 0xFF && rb[2] == 0xEE);
    CHECK(s2.st == S_IDLE && s2.tx_pos == 3);
    CHECK(i2c_read(&b, 0x68, rb, 0) == I2C_ERR_ARG);
    CHECK(i2c_read(&b, 0x29, rb, 2) == I2C_ERR_NACK_ADDR);
    uint8_t found[8] = {0};
    CHECK(i2c_scan(&b, found, 8) == 2 && found[0] == 0x50 && found[1] == 0x68);
    memset(found, 0, sizeof found);
    CHECK(i2c_scan(&b, found, 1) == 2 && found[0] == 0x50 && found[1] == 0);  /* cap 지킴 */

    /* 버스 busy: 고장 노드가 SDA 를 LOW 로 붙잡음 → 클럭 하나도 내지 않고 에러 */
    b.sda_drv[fault] = 0; bus_tick(&b);
    uint32_t t0 = b.ticks;
    CHECK(i2c_write(&b, 0x50, w3, 1, &na) == I2C_ERR_BUS_BUSY && b.ticks == t0);
    CHECK(i2c_read(&b, 0x68, rb, 1) == I2C_ERR_BUS_BUSY && i2c_scan(&b, found, 8) == I2C_ERR_BUS_BUSY);
    b.sda_drv[fault] = 1; bus_tick(&b);                    /* 놓으면 (다른 노드 눈엔 STOP) 복귀 */
    s1.rx_len = 0; s1.rx_cap = 16;
    CHECK(i2c_write(&b, 0x50, w3, 1, &na) == I2C_OK && s1.rx_len == 1);

    /* 랜덤 트랜잭션 400개: 대상 0x50 / 0x68 / 0x3C(없음), 쓰기·읽기, 길이 1..8 */
    trace_reset(&b);
    bad = 0;
    int nwr = 0, nrd = 0, nnak = 0;
    for (int it = 0; it < 400; it++) {
        uint32_t r = xorshift32();
        uint8_t tgt = (r & 3) == 0 ? 0x3C : (r & 1) ? 0x50 : 0x68;
        Slave *sl = tgt == 0x50 ? &s1 : &s2;
        size_t len = 1 + (r >> 4) % 8;
        uint8_t data[8], got[8];
        for (size_t i = 0; i < len; i++) data[i] = (uint8_t)xorshift32();
        if (r & 4) {
            s1.rx_len = s2.rx_len = 0;
            int e = i2c_write(&b, tgt, data, len, &na);
            if (tgt == 0x3C) { bad += e != I2C_ERR_NACK_ADDR; nnak++; }
            else { bad += e != I2C_OK || na != len || sl->rx_len != len || memcmp(sl->rx, data, len) != 0; nwr++; }
        } else {
            memcpy(s1.tx, data, len); memcpy(s2.tx, data, len);
            s1.tx_len = s2.tx_len = len; s1.tx_pos = s2.tx_pos = 0;
            int e = i2c_read(&b, tgt, got, len);
            if (tgt == 0x3C) { bad += e != I2C_ERR_NACK_ADDR; nnak++; }
            else { bad += e != I2C_OK || memcmp(got, data, len) != 0 || sl->tx_pos != len; nrd++; }
        }
    }
    printf("random: %d writes, %d reads, %d to absent 0x3C, mismatches=%d, SDA moves while SCL high=%u\n",
           nwr, nrd, nnak, bad, (unsigned)b.sda_moves_scl_high);
    CHECK(bad == 0);
    CHECK(b.sda_moves_scl_high == 400 * 2);                /* 트랜잭션마다 START 1 + STOP 1 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
