/* COMM-B06 추가 연습 — 한 번에 검증
 *
 * 본문 solution.c 의 버스·마스터·슬레이브·드라이버를 그대로 담고 (연습 2 용 busy 3줄만 추가), 아래 연습을 더한다.
 * 연습 1: 트레이스 디코더 (로직 애널라이저처럼 S / 바이트 / A·N / P 출력)
 * 연습 2: EEPROM 쓰기 사이클 동안 주소 NACK → ACK polling (i2c_wait_ready)
 * 연습 3: 트랜잭션 길이 — 모델 틱 수와 실제 SCL 시간 계산
 * 연습 4: 마지막 읽기 바이트를 ACK 하는 버그 → STOP 실패, 버스 busy
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
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
    uint32_t busy_after_write, busy_left; bool wrote;  /* 연습 2: 쓰기 뒤 내부 쓰기 사이클 */
};

/* ---------------- Part A: wired-AND + 비트뱅 마스터 ---------------- */

/* 모든 노드 드라이버의 AND. 아무도 안 당기면 풀업 때문에 1. */
static void bus_resolve(Bus *b) {
    uint8_t sda = 1, scl = 1;
    for (int i = 0; i < b->nnodes; i++) {
        sda &= b->sda_drv[i];
        scl &= b->scl_drv[i];
    }
    b->sda = sda;
    b->scl = scl;
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
    if (!b->scl) { set_sda(b, 1); set_scl(b, 1); }
    set_sda(b, 0);
    set_scl(b, 0);
}

/* STOP: (SCL=0 에서) SDA 0 → SCL 1 → SDA 0→1 */
static void m_stop(Bus *b) {
    set_sda(b, 0);
    set_scl(b, 1);
    set_sda(b, 1);
}

/* 비트 쓰기: SCL=0 인 동안 SDA 를 정하고 → SCL 1 (슬레이브가 샘플) → SCL 0 */
static void m_write_bit(Bus *b, int bit) {
    set_sda(b, bit);
    set_scl(b, 1);
    set_scl(b, 0);
}

/* 비트 읽기: SDA 를 놓고 → SCL 1 → SDA 읽기 → SCL 0 */
static int m_read_bit(Bus *b) {
    set_sda(b, 1);
    set_scl(b, 1);
    int v = b->sda;
    set_scl(b, 0);
    return v;
}

/* MSB first 8비트 + 9번째 클럭에 ACK 읽기. 반환: true = ACK(SDA=0) */
static bool m_write_byte(Bus *b, uint8_t byte) {
    for (int i = 7; i >= 0; i--) m_write_bit(b, (byte >> i) & 1);
    return m_read_bit(b) == 0;
}

/* 8비트 읽기 + 9번째 클럭에 ACK(0) 또는 NACK(1) 쓰기 */
static uint8_t m_read_byte(Bus *b, bool ack) {
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) v = (uint8_t)(v << 1 | m_read_bit(b));
    m_write_bit(b, ack ? 0 : 1);
    return v;
}

/* ---------------- Part B: 슬레이브 상태 머신 ---------------- */

/* 매 틱 호출. 규칙: SCL=1 동안 SDA 변화 = START(↓)/STOP(↑), SCL↑ 에서 샘플, SCL↓ 에서만 SDA 변경. */
static void slave_on_tick(Slave *s, Bus *b) {
    uint8_t scl = b->scl, sda = b->sda;
    if (s->busy_left) s->busy_left--;                          /* 연습 2 */
    uint8_t *out = &b->sda_drv[s->node];
    if (s->prev_scl && scl && s->prev_sda != sda) {          /* SCL high 중 SDA 변화 */
        if (!sda) { s->starts++; s->st = S_ADDR; s->nbits = 0; s->sh = 0; }
        else      { s->stops++;  s->st = S_IDLE;
                    if (s->wrote) { s->busy_left = s->busy_after_write; s->wrote = false; } } /* 연습 2 */
        *out = 1;
    } else if (!s->prev_scl && scl) {                         /* SCL ↑ : 샘플 */
        if (s->st == S_ADDR || s->st == S_RX) { s->sh = (uint8_t)(s->sh << 1 | sda); s->nbits++; }
        else if (s->st == S_TX) s->nbits++;
        else if (s->st == S_TX_ACK) s->master_ack = (sda == 0);
    } else if (s->prev_scl && !scl) {                         /* SCL ↓ : SDA 바꿀 차례 */
        switch (s->st) {
        case S_ADDR:
            if (s->nbits == 8) {
                if ((s->sh >> 1) == s->addr && s->busy_left == 0) {   /* 연습 2: busy 면 NACK */
                    s->rw = s->sh & 1; s->addr_matches++; *out = 0; s->st = S_ADDR_ACK;
                } else {
                    s->st = S_IDLE;                             /* 남의 주소: 다음 START 까지 무시 */
                }
            }
            break;
        case S_RX:
            if (s->nbits == 8) {
                if (s->rx_len < s->rx_cap) { s->rx[s->rx_len++] = s->sh; *out = 0; s->wrote = true; } /* ACK */
                s->st = S_RX_ACK;                               /* 꽉 찼으면 놓아 둠 = NACK */
            }
            break;
        case S_ADDR_ACK:
        case S_RX_ACK:
            s->nbits = 0; s->sh = 0;
            if (s->st == S_ADDR_ACK && s->rw) {                 /* 읽기: 첫 바이트 MSB 를 바로 낸다 */
                s->sh = s->tx_pos < s->tx_len ? s->tx[s->tx_pos++] : 0xFF;
                *out = (uint8_t)(s->sh >> 7); s->st = S_TX;
            } else {
                *out = 1; s->st = S_RX;                         /* ACK 를 놓고 다음 바이트 수신 */
            }
            break;
        case S_TX:
            if (s->nbits < 8) *out = (uint8_t)((s->sh >> (7 - s->nbits)) & 1);
            else { *out = 1; s->st = S_TX_ACK; }                /* 9번째 클럭은 마스터 차례 */
            break;
        case S_TX_ACK:
            if (s->master_ack) {
                s->sh = s->tx_pos < s->tx_len ? s->tx[s->tx_pos++] : 0xFF;
                s->nbits = 0; *out = (uint8_t)(s->sh >> 7); s->st = S_TX;
            } else {
                *out = 1; s->st = S_IDLE;                       /* NACK = 그만. STOP 기다림 */
            }
            break;
        default:
            break;
        }
    }
    s->prev_scl = scl;
    s->prev_sda = sda;
}

/* ---------------- Part C: 드라이버 API ---------------- */
enum { I2C_OK = 0, I2C_ERR_ARG = -1, I2C_ERR_BUS_BUSY = -2, I2C_ERR_NACK_ADDR = -3, I2C_ERR_NACK_DATA = -4 };

/* START, addr7+W, data[0..n). NACK 이면 즉시 STOP 하고 에러. *n_acked = ACK 받은 데이터 바이트 수. */
static int i2c_write(Bus *b, uint8_t addr7, const uint8_t *data, size_t n, size_t *n_acked) {
    size_t k = 0;
    if (n_acked) *n_acked = 0;
    if (addr7 > 0x7F || (n > 0 && !data)) return I2C_ERR_ARG;
    if (!b->sda || !b->scl) return I2C_ERR_BUS_BUSY;        /* START 전에 버스가 idle 인지 */
    m_start(b);
    if (!m_write_byte(b, (uint8_t)(addr7 << 1))) { m_stop(b); return I2C_ERR_NACK_ADDR; }
    while (k < n && m_write_byte(b, data[k])) k++;
    m_stop(b);
    if (n_acked) *n_acked = k;
    return k == n ? I2C_OK : I2C_ERR_NACK_DATA;
}

/* START, addr7+R, n 바이트 읽기 (마지막 바이트만 NACK), STOP. n == 0 은 I2C 로 불가 → ARG */
static int i2c_read(Bus *b, uint8_t addr7, uint8_t *buf, size_t n) {
    if (addr7 > 0x7F || n == 0 || !buf) return I2C_ERR_ARG;
    if (!b->sda || !b->scl) return I2C_ERR_BUS_BUSY;
    m_start(b);
    if (!m_write_byte(b, (uint8_t)(addr7 << 1 | 1))) { m_stop(b); return I2C_ERR_NACK_ADDR; }
    for (size_t i = 0; i < n; i++) buf[i] = m_read_byte(b, i + 1 < n);
    m_stop(b);
    return I2C_OK;
}

/* 0x08..0x77 에 주소만 보내 본다 (START, addr+W, STOP). ACK 한 주소를 found[] 에 cap 개까지 저장.
 * 반환 = ACK 한 주소의 총 개수 (> cap 이면 잘렸다는 뜻), 버스 busy 면 I2C_ERR_BUS_BUSY. */
static int i2c_scan(Bus *b, uint8_t *found, size_t cap) {
    int cnt = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        int r = i2c_write(b, a, NULL, 0, NULL);
        if (r == I2C_ERR_BUS_BUSY) return r;
        if (r == I2C_OK) {
            if ((size_t)cnt < cap) found[cnt] = a;
            cnt++;
        }
    }
    return cnt;
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

/* ---------------- 연습 1: 트레이스 디코더 ---------------- */
/* 토큰 하나를 공백으로 이어 붙인다. 용량(널 포함) 부족이면 -1 */
static int put_tok(char *out, size_t cap, size_t *len, const char *tok) {
    size_t t = strlen(tok), sep = *len ? 1 : 0;
    if (*len + sep + t + 1 > cap) return -1;
    if (sep) out[(*len)++] = ' ';
    memcpy(out + *len, tok, t);
    *len += t;
    out[*len] = '\0';
    return 0;
}

/* scl/sda = '0'/'1' 문자열 n 칸. 출력 예: "S A0 A DE A P".
 * S/P = SCL high 중 SDA ↓/↑, 바이트 = SCL ↑ 8개, A/N = 9번째 SCL ↑ 의 SDA. 잘린 바이트는 버린다.
 * 반환 = 출력 길이, 용량 부족이면 -1 */
static int decode_trace(const char *scl, const char *sda, size_t n, char *out, size_t cap) {
    size_t len = 0;
    int nb = -1;                /* -1 = START 전/STOP 뒤 (무시), 0..8 = 이번 바이트에서 받은 비트 수 */
    unsigned sh = 0;
    char tok[4];
    if (cap == 0) return -1;
    out[0] = '\0';
    for (size_t i = 1; i < n; i++) {
        int c0 = scl[i - 1] == '1', c1 = scl[i] == '1', d0 = sda[i - 1] == '1', d1 = sda[i] == '1';
        if (c0 && c1 && d0 != d1) {
            if (put_tok(out, cap, &len, d1 ? "P" : "S")) return -1;
            nb = d1 ? -1 : 0;
            sh = 0;
        } else if (!c0 && c1 && nb >= 0) {
            if (nb < 8) { sh = sh << 1 | (unsigned)d1; nb++; }
            else {
                snprintf(tok, sizeof tok, "%02X", sh & 0xFFu);
                if (put_tok(out, cap, &len, tok) || put_tok(out, cap, &len, d1 ? "N" : "A")) return -1;
                nb = 0;
                sh = 0;
            }
        }
    }
    return (int)len;
}

/* ---------------- 연습 2: ACK polling ---------------- */
enum { I2C_ERR_TIMEOUT = -5 };

/* 주소만 보내 ACK 가 올 때까지 최대 max_polls 번. 반환 = ACK 받은 시도 번호(1..), 아니면 에러 */
static int i2c_wait_ready(Bus *b, uint8_t addr7, int max_polls) {
    for (int k = 1; k <= max_polls; k++) {
        int r = i2c_write(b, addr7, NULL, 0, NULL);
        if (r == I2C_OK) return k;
        if (r != I2C_ERR_NACK_ADDR) return r;           /* busy 버스 등은 그대로 올린다 */
    }
    return I2C_ERR_TIMEOUT;
}

/* 이 모델에서 필요한 polling 횟수: 프로브 = 32틱, ACK 결정 = 프로브의 26번째 틱(8번째 SCL↓).
 * busy B 틱이면 k 번째 프로브 결정 시점 32(k-1)+26 >= B 인 최소 k */
static int polls_needed(uint32_t busy) {
    return busy <= 26 ? 1 : 1 + (int)((busy - 26 + 31) / 32);
}

/* ---------------- 연습 3: 트랜잭션 길이 ---------------- */
/* 모델 틱 수: START 2 + 바이트당 27(9비트 × 3틱) + STOP 3. 데이터 n 바이트 + 주소 1 바이트 */
static uint32_t model_ticks(size_t n_data) { return 2u + 27u * (uint32_t)(n_data + 1) + 3u; }

/* 실제 SCL 클럭 시간 (START/STOP 의 setup·hold, 버스 free 시간은 뺀 하한): 바이트당 9 클럭 */
static uint64_t scl_time_ns(size_t bytes_on_wire, uint32_t f_scl_hz) {
    return 9ull * bytes_on_wire * 1000000000ull / f_scl_hz;
}

/* ---------------- 연습 4: 마지막 바이트까지 ACK 하는 버그 ---------------- */
static int i2c_read_bad(Bus *b, uint8_t addr7, uint8_t *buf, size_t n) {
    if (addr7 > 0x7F || n == 0 || !buf) return I2C_ERR_ARG;
    if (!b->sda || !b->scl) return I2C_ERR_BUS_BUSY;
    m_start(b);
    if (!m_write_byte(b, (uint8_t)(addr7 << 1 | 1))) { m_stop(b); return I2C_ERR_NACK_ADDR; }
    for (size_t i = 0; i < n; i++) buf[i] = m_read_byte(b, true);   /* 버그: 마지막도 ACK */
    m_stop(b);
    return I2C_OK;
}

int main(void) {
    Bus b; Slave s1, s2;
    char txt[256];
    size_t na;

    /* ===== 연습 1: 디코더 ===== */
    bus_init(&b); bus_add_slave(&b, &s1, 0x50, 4); bus_add_slave(&b, &s2, 0x68, 16);
    trace_reset(&b);
    CHECK(i2c_write(&b, 0x50, NULL, 0, NULL) == I2C_OK);
    CHECK(decode_trace(b.tr_scl, b.tr_sda, b.tr_len, txt, sizeof txt) > 0 && strcmp(txt, "S A0 A P") == 0);
    trace_reset(&b);
    CHECK(i2c_write(&b, 0x3C, NULL, 0, NULL) == I2C_ERR_NACK_ADDR);
    decode_trace(b.tr_scl, b.tr_sda, b.tr_len, txt, sizeof txt);
    CHECK(strcmp(txt, "S 78 N P") == 0);
    const uint8_t w3[3] = {0xDE, 0xAD, 0x01};
    trace_reset(&b);
    CHECK(i2c_write(&b, 0x50, w3, 3, &na) == I2C_OK);
    decode_trace(b.tr_scl, b.tr_sda, b.tr_len, txt, sizeof txt);
    CHECK(strcmp(txt, "S A0 A DE A AD A 01 A P") == 0);
    s1.rx_len = 0; s1.rx_cap = 1;
    trace_reset(&b);
    CHECK(i2c_write(&b, 0x50, w3, 3, &na) == I2C_ERR_NACK_DATA && na == 1);
    decode_trace(b.tr_scl, b.tr_sda, b.tr_len, txt, sizeof txt);
    CHECK(strcmp(txt, "S A0 A DE A AD N P") == 0);
    s1.rx_cap = 16;
    uint8_t rb[8];
    s2.tx[0] = 0xC0; s2.tx[1] = 0xFF; s2.tx_len = 2; s2.tx_pos = 0;
    trace_reset(&b);
    CHECK(i2c_read(&b, 0x68, rb, 2) == I2C_OK);
    decode_trace(b.tr_scl, b.tr_sda, b.tr_len, txt, sizeof txt);
    CHECK(strcmp(txt, "S D1 A C0 A FF N P") == 0);
    trace_reset(&b);                                        /* 본문의 규칙 위반 마스터 */
    m_start(&b);
    m_write_byte(&b, 0x50 << 1);
    m_write_byte_bad(&b, 0xA5);
    m_stop(&b);
    decode_trace(b.tr_scl, b.tr_sda, b.tr_len, txt, sizeof txt);
    printf("decoded bad master: %s\n", txt);
    CHECK(strcmp(txt, "S A0 A S P S P S P P") == 0);
    CHECK(decode_trace(b.tr_scl, b.tr_sda, b.tr_len, txt, 8) == -1);  /* 용량 부족 */
    /* 랜덤 200개: 디코드 결과 == 기대 문자열 */
    int bad = 0;
    for (int it = 0; it < 200; it++) {
        uint32_t r = xorshift32();
        size_t len = 1 + (r >> 8) % 8;
        uint8_t data[8];
        char exp[256]; size_t el = 0;
        for (size_t i = 0; i < len; i++) data[i] = (uint8_t)xorshift32();
        s1.rx_len = 0;
        memcpy(s2.tx, data, len); s2.tx_len = len; s2.tx_pos = 0;
        trace_reset(&b);
        char tok[4];
        put_tok(exp, sizeof exp, &el, "S");
        if (r & 1) {
            i2c_write(&b, 0x50, data, len, &na);
            put_tok(exp, sizeof exp, &el, "A0"); put_tok(exp, sizeof exp, &el, "A");
            for (size_t i = 0; i < len; i++) {
                snprintf(tok, sizeof tok, "%02X", data[i]);
                put_tok(exp, sizeof exp, &el, tok); put_tok(exp, sizeof exp, &el, "A");
            }
        } else {
            i2c_read(&b, 0x68, rb, len);
            put_tok(exp, sizeof exp, &el, "D1"); put_tok(exp, sizeof exp, &el, "A");
            for (size_t i = 0; i < len; i++) {
                snprintf(tok, sizeof tok, "%02X", data[i]);
                put_tok(exp, sizeof exp, &el, tok); put_tok(exp, sizeof exp, &el, i + 1 < len ? "A" : "N");
            }
        }
        put_tok(exp, sizeof exp, &el, "P");
        decode_trace(b.tr_scl, b.tr_sda, b.tr_len, txt, sizeof txt);
        bad += strcmp(txt, exp) != 0;
    }
    CHECK(bad == 0);

    /* ===== 연습 2: ACK polling ===== */
    bus_init(&b); bus_add_slave(&b, &s1, 0x50, 16);
    uint8_t found[4];
    CHECK(i2c_scan(&b, found, 4) == 1 && found[0] == 0x50);
    s1.busy_after_write = 200;
    CHECK(i2c_write(&b, 0x50, w3, 2, &na) == I2C_OK);
    CHECK(i2c_wait_ready(&b, 0x50, 100) == 7 && polls_needed(200) == 7);
    CHECK(i2c_write(&b, 0x50, w3, 2, &na) == I2C_OK);
    CHECK(i2c_wait_ready(&b, 0x50, 3) == I2C_ERR_TIMEOUT);      /* 3번 = 96틱 소모, 남은 busy 104 */
    CHECK(i2c_wait_ready(&b, 0x50, 100) == 4 && polls_needed(104) == 4);
    CHECK(i2c_write(&b, 0x50, w3, 1, &na) == I2C_OK);           /* 준비되면 바로 다음 쓰기 OK */
    CHECK(i2c_wait_ready(&b, 0x3C, 5) == I2C_ERR_TIMEOUT);      /* 없는 디바이스 = 영원히 NACK */
    i2c_wait_ready(&b, 0x50, 1000);                              /* 위 쓰기의 busy 를 끝낸다 */
    bad = 0;
    for (uint32_t busy = 0; busy <= 400; busy++) {              /* 공식 vs 시뮬레이션 전수 */
        s1.busy_after_write = busy;
        s1.rx_len = 0;
        bad += i2c_write(&b, 0x50, w3, 1, &na) != I2C_OK;
        bad += i2c_wait_ready(&b, 0x50, 1000) != polls_needed(busy);
    }
    CHECK(bad == 0);

    /* ===== 연습 3: 길이 ===== */
    bus_init(&b); bus_add_slave(&b, &s1, 0x50, 16);
    bad = 0;
    for (size_t n = 0; n <= 8; n++) {
        uint32_t t0 = b.ticks;
        s1.rx_len = 0;
        i2c_write(&b, 0x50, w3, n < 3 ? n : 3, &na);
        if (n <= 3 && b.ticks - t0 != model_ticks(n)) bad++;
        if (n >= 1) {
            t0 = b.ticks;
            i2c_read(&b, 0x50, rb, n);
            if (b.ticks - t0 != model_ticks(n)) bad++;
        }
    }
    CHECK(bad == 0);
    CHECK(model_ticks(0) == 32);
    CHECK(scl_time_ns(5, 100000) == 450000);   /* 레지스터 2바이트 읽기: addr+W, reg, addr+R, d0, d1 */
    CHECK(scl_time_ns(5, 400000) == 112500);
    CHECK(scl_time_ns(5, 1000000) == 45000);
    CHECK(1000000000ull / scl_time_ns(5, 400000) == 8888);      /* 400 kHz 에서 초당 최대 ~8888 번 */

    /* ===== 연습 4: 마지막 바이트 ACK 버그 ===== */
    bus_init(&b); bus_add_slave(&b, &s2, 0x68, 16);
    s2.tx[0] = 0x11; s2.tx[1] = 0x22; s2.tx[2] = 0x05; s2.tx_len = 3;   /* 다음 바이트 MSB = 0 */
    uint32_t sp0 = s2.stops;
    trace_reset(&b);
    CHECK(i2c_read_bad(&b, 0x68, rb, 2) == I2C_OK && rb[0] == 0x11 && rb[1] == 0x22);
    CHECK(b.scl == 1 && b.sda == 0);                            /* 슬레이브가 SDA 를 쥐고 있다 */
    trace_print(&b, "bad read: ACK on last byte, next byte MSB=0");
    CHECK(s2.st == S_TX && s2.stops == sp0);                    /* STOP 을 못 봤다 */
    CHECK(i2c_write(&b, 0x68, w3, 1, &na) == I2C_ERR_BUS_BUSY); /* 다음 트랜잭션 불가 → COMM-M03 */
    bus_init(&b); bus_add_slave(&b, &s2, 0x68, 16);
    s2.tx[0] = 0x11; s2.tx[1] = 0x22; s2.tx[2] = 0x85; s2.tx_len = 3;   /* MSB = 1 → 우연히 동작 */
    CHECK(i2c_read_bad(&b, 0x68, rb, 2) == I2C_OK && b.sda == 1 && b.scl == 1 && s2.st == S_IDLE);
    CHECK(s2.tx_pos == 3);                                      /* 하지만 한 바이트 더 꺼내 갔다 (FIFO 손실) */
    bus_init(&b); bus_add_slave(&b, &s2, 0x68, 16);
    s2.tx[0] = 0x11; s2.tx[1] = 0x22; s2.tx[2] = 0x05; s2.tx_len = 3;
    CHECK(i2c_read(&b, 0x68, rb, 2) == I2C_OK && b.sda == 1 && b.scl == 1 && s2.tx_pos == 2);  /* 정답 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
