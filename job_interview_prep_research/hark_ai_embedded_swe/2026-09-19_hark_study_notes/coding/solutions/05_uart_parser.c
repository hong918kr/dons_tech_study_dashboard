/* 05_uart_parser.c — byte-at-a-time UART framed-protocol parser (solution)
 *
 * Frame layout (study/S01 Q11과 동일):
 *   | SOF 0xA5 | LEN | CMD | PAYLOAD[LEN] | CRC_H | CRC_L |
 *
 *   - CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, xorout 0)
 *     over LEN, CMD, PAYLOAD. 전송은 big-endian (CRC_H 먼저).
 *   - LEN <= PKT_MAX_PAYLOAD (64). 넘으면 프레임 폐기 + err_len.
 *   - 바이트 사이 간격이 PKT_TIMEOUT_MS를 넘으면 반쯤 받은 프레임 폐기 + err_timeout.
 *
 * 실행 환경 가정: UART RX ISR이 링버퍼에 바이트를 넣고, task가 한 바이트씩
 * parser_feed()를 호출한다. 동적 할당 없음, 블로킹 없음, 바이트당 O(1).
 *
 * now_ms는 free-running millisecond tick이라고 가정한다. 실제 타깃에서는
 * HAL_GetTick() / xTaskGetTickCount() 같은 것이지만, 여기서는 벤더 헤더를
 * 쓰지 않기 위해 테스트가 직접 tick 값을 넘긴다(예시용 가짜 tick).
 *
 * build: cc -std=c11 -Wall -Wextra -O2 05_uart_parser.c -o sol_05
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 프로토콜 상수                                                        */
/* ------------------------------------------------------------------ */

#define PKT_SOF          0xA5u
#define PKT_MAX_PAYLOAD  64u
#define PKT_TIMEOUT_MS   50u

/* ------------------------------------------------------------------ */
/* 타입                                                                 */
/* ------------------------------------------------------------------ */

typedef enum {
    ST_SOF,      /* SOF 사냥 중. 프레임 밖 */
    ST_LEN,      /* LEN 바이트 대기 */
    ST_CMD,      /* CMD 바이트 대기 */
    ST_PAYLOAD,  /* payload LEN개 수집 중 */
    ST_CRC_H,    /* CRC 상위 바이트 대기 */
    ST_CRC_L     /* CRC 하위 바이트 대기 */
} pstate_t;

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[PKT_MAX_PAYLOAD];
} packet_t;

typedef struct {
    pstate_t st;
    packet_t pkt;
    uint8_t  pos;        /* payload에 지금까지 담은 개수 */
    uint16_t crc;        /* 수신하며 누적 계산한 CRC */
    uint16_t rx_crc;     /* 프레임에 실려 온 CRC */
    uint32_t last_ms;    /* 직전 바이트의 tick */
    uint32_t err_crc;    /* CRC 불일치 횟수 */
    uint32_t err_len;    /* LEN 초과 횟수 */
    uint32_t err_timeout;/* inter-byte timeout 횟수 */
} parser_t;

/* ------------------------------------------------------------------ */
/* CRC-16/CCITT-FALSE, 바이트 스트리밍                                   */
/* ------------------------------------------------------------------ */

static uint16_t crc16_byte(uint16_t crc, uint8_t b)
{
    crc ^= (uint16_t)((uint16_t)b << 8);
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000u) ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u)
                              : (uint16_t)(crc << 1);
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/* 파서                                                                 */
/* ------------------------------------------------------------------ */

void parser_init(parser_t *p)
{
    p->st          = ST_SOF;
    p->pos         = 0u;
    p->crc         = 0xFFFFu;
    p->rx_crc      = 0u;
    p->last_ms     = 0u;
    p->err_crc     = 0u;
    p->err_len     = 0u;
    p->err_timeout = 0u;
    p->pkt.cmd     = 0u;
    p->pkt.len     = 0u;
    memset(p->pkt.payload, 0, sizeof p->pkt.payload);
}

/* 한 바이트를 먹인다. 완성된 패킷이 있으면 true를 반환하고 결과는 p->pkt에 있다.
 * 반환이 true인 그 호출 직후에만 p->pkt가 유효하다(다음 프레임이 덮어쓴다). */
bool parser_feed(parser_t *p, uint8_t b, uint32_t now_ms)
{
    /* (1) inter-byte timeout: 프레임 중간에서 너무 오래 끊겼으면 통째로 버린다.
     *     unsigned 뺄셈이라 tick이 2^32에서 wrap해도 올바르다. */
    if (p->st != ST_SOF && (uint32_t)(now_ms - p->last_ms) > PKT_TIMEOUT_MS) {
        p->err_timeout++;
        p->st = ST_SOF;
    }
    p->last_ms = now_ms;

    /* (2) 상태 전이 */
    switch (p->st) {
    case ST_SOF:
        /* SOF가 아닌 바이트는 전부 버린다. 이것이 garbage 복구의 전부다. */
        if (b == PKT_SOF) {
            p->crc = 0xFFFFu;          /* 프레임 시작 == CRC 리셋 */
            p->st  = ST_LEN;
        }
        break;

    case ST_LEN:
        if (b > PKT_MAX_PAYLOAD) {
            /* 버퍼를 건드리기 전에 길이를 검증한다. overflow 1차 방어선. */
            p->err_len++;
            /* 이 바이트 자체가 SOF라면 앞의 0xA5는 noise였고 이게 진짜
             * 프레임 시작일 수 있다. 그 자리에서 재동기화한다.
             * crc는 아직 0xFFFF 그대로라 다시 리셋할 필요가 없다. */
            p->st = (b == PKT_SOF) ? ST_LEN : ST_SOF;
            break;
        }
        p->pkt.len = b;
        p->crc     = crc16_byte(p->crc, b);
        p->st      = ST_CMD;
        break;

    case ST_CMD:
        p->pkt.cmd = b;
        p->crc     = crc16_byte(p->crc, b);
        p->pos     = 0u;
        /* LEN == 0 프레임(명령만)은 PAYLOAD 상태를 건너뛴다. */
        p->st      = (p->pkt.len == 0u) ? ST_CRC_H : ST_PAYLOAD;
        break;

    case ST_PAYLOAD:
        /* pos < len <= PKT_MAX_PAYLOAD가 ST_LEN 검증으로 보장된다. */
        p->pkt.payload[p->pos++] = b;
        p->crc = crc16_byte(p->crc, b);
        if (p->pos == p->pkt.len) {
            p->st = ST_CRC_H;
        }
        break;

    case ST_CRC_H:
        p->rx_crc = (uint16_t)((uint16_t)b << 8);
        p->st     = ST_CRC_L;
        break;

    case ST_CRC_L:
        p->rx_crc = (uint16_t)(p->rx_crc | b);
        p->st     = ST_SOF;            /* 성공이든 실패든 hunt로 돌아간다 */
        if (p->rx_crc == p->crc) {
            return true;
        }
        p->err_crc++;
        break;
    }

    return false;
}

/* ================================================================== */
/* 여기부터는 테스트 코드                                                */
/* ================================================================== */

static uint16_t crc16_buf(uint16_t crc, const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc = crc16_byte(crc, d[i]);
    }
    return crc;
}

/* 올바른 프레임을 out에 만들고 길이를 반환한다. out은 최소 69바이트. */
static size_t frame_build(uint8_t *out, uint8_t cmd, const uint8_t *pl, uint8_t len)
{
    size_t n = 0u;
    out[n++] = (uint8_t)PKT_SOF;
    out[n++] = len;
    out[n++] = cmd;
    for (uint8_t i = 0u; i < len; i++) {
        out[n++] = pl[i];
    }
    uint16_t c = crc16_buf(0xFFFFu, out + 1, (size_t)len + 2u);
    out[n++] = (uint8_t)(c >> 8);
    out[n++] = (uint8_t)(c & 0xFFu);
    return n;
}

/* 바이트 열을 dt ms 간격으로 먹이고 완성된 패킷 수를 반환한다.
 * last가 NULL이 아니면 마지막으로 완성된 패킷을 복사해 둔다. */
static unsigned feed_all(parser_t *p, const uint8_t *b, size_t n,
                         uint32_t *t, uint32_t dt, packet_t *last)
{
    unsigned got = 0u;
    for (size_t i = 0u; i < n; i++) {
        if (parser_feed(p, b[i], *t)) {
            got++;
            if (last != NULL) {
                *last = p->pkt;
            }
        }
        *t += dt;
    }
    return got;
}

static void test_crc_check_value(void)
{
    const uint8_t s[] = "123456789";
    uint16_t c = crc16_buf(0xFFFFu, s, sizeof s - 1u);
    assert(c == 0x29B1u);
    printf("  ok  CRC-16/CCITT-FALSE check value 0x29B1\n");
}

static void test_happy_path(void)
{
    parser_t p;
    parser_init(&p);

    const uint8_t pl[3] = { 0x01u, 0x02u, 0x03u };
    uint8_t f[69];
    size_t n = frame_build(f, 0x10u, pl, 3u);
    assert(n == 8u);

    uint32_t t = 1000u;
    packet_t got;
    unsigned cnt = 0u;
    for (size_t i = 0u; i < n; i++) {
        bool done = parser_feed(&p, f[i], t);
        /* 마지막 바이트에서만 true여야 한다 */
        assert(done == (i == n - 1u));
        if (done) {
            got = p.pkt;
            cnt++;
        }
        t += 1u;
    }
    assert(cnt == 1u);
    assert(got.cmd == 0x10u);
    assert(got.len == 3u);
    assert(memcmp(got.payload, pl, 3u) == 0);
    assert(p.st == ST_SOF);
    assert(p.err_crc == 0u && p.err_len == 0u && p.err_timeout == 0u);
    printf("  ok  happy path: 1 packet, cmd=0x10 len=3, no errors\n");
}

static void test_zero_length_and_max_length(void)
{
    parser_t p;
    uint8_t f[69];
    packet_t got;
    uint32_t t;

    /* LEN == 0: PAYLOAD 상태를 건너뛴다 */
    parser_init(&p);
    size_t n = frame_build(f, 0x77u, NULL, 0u);
    assert(n == 5u);
    t = 0u;
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);
    assert(got.len == 0u && got.cmd == 0x77u);

    /* LEN == 64: 경계값, 허용되어야 한다 */
    uint8_t pl[PKT_MAX_PAYLOAD];
    for (unsigned i = 0u; i < PKT_MAX_PAYLOAD; i++) {
        pl[i] = (uint8_t)(i * 7u + 1u);
    }
    parser_init(&p);
    n = frame_build(f, 0x20u, pl, (uint8_t)PKT_MAX_PAYLOAD);
    assert(n == 69u);
    t = 0u;
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);
    assert(got.len == PKT_MAX_PAYLOAD);
    assert(memcmp(got.payload, pl, PKT_MAX_PAYLOAD) == 0);
    assert(p.err_len == 0u);
    printf("  ok  boundary: LEN=0 skips payload, LEN=64 accepted\n");
}

static void test_bad_crc(void)
{
    parser_t p;
    parser_init(&p);

    const uint8_t pl[2] = { 0xDEu, 0xADu };
    uint8_t f[69];
    size_t n = frame_build(f, 0x31u, pl, 2u);
    f[n - 1u] = (uint8_t)(f[n - 1u] ^ 0xFFu);   /* CRC_L 손상 */

    uint32_t t = 0u;
    assert(feed_all(&p, f, n, &t, 1u, NULL) == 0u);
    assert(p.err_crc == 1u);
    assert(p.err_len == 0u && p.err_timeout == 0u);
    assert(p.st == ST_SOF);                     /* 실패해도 hunt로 복귀 */

    /* 바로 뒤의 정상 프레임은 정상 수신되어야 한다 */
    packet_t got;
    n = frame_build(f, 0x32u, pl, 2u);
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);
    assert(got.cmd == 0x32u);
    assert(p.err_crc == 1u);
    printf("  ok  bad CRC: dropped, err_crc=1, next frame still parses\n");
}

static void test_oversize_length(void)
{
    parser_t p;
    parser_init(&p);

    /* LEN = 65 (한계 바로 위) */
    const uint8_t bad[4] = { (uint8_t)PKT_SOF, 65u, 0x00u, 0x00u };
    uint32_t t = 0u;
    assert(feed_all(&p, bad, sizeof bad, &t, 1u, NULL) == 0u);
    assert(p.err_len == 1u);
    assert(p.st == ST_SOF);

    /* LEN = 255 (payload 버퍼를 넘기려는 전형적 공격/노이즈) */
    const uint8_t bad2[2] = { (uint8_t)PKT_SOF, 255u };
    assert(feed_all(&p, bad2, sizeof bad2, &t, 1u, NULL) == 0u);
    assert(p.err_len == 2u);

    /* 그 뒤 정상 프레임 */
    const uint8_t pl[1] = { 0x5Au };
    uint8_t f[69];
    packet_t got;
    size_t n = frame_build(f, 0x41u, pl, 1u);
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);
    assert(got.cmd == 0x41u && got.len == 1u && got.payload[0] == 0x5Au);
    assert(p.err_len == 2u && p.err_crc == 0u);
    printf("  ok  oversize LEN (65, 255) rejected before touching payload\n");
}

static void test_resync_sof_in_len_slot(void)
{
    parser_t p;
    parser_init(&p);

    /* noise 0xA5가 하나 앞에 붙어 LEN 자리에 진짜 SOF가 온 경우:
     *   A5 | A5 03 10 01 02 03 CRC_H CRC_L
     * 두 번째 A5는 65 이상이라 err_len을 올리지만 그 자리에서 ST_LEN을
     * 유지하므로 프레임을 잃지 않는다. */
    const uint8_t pl[3] = { 0x01u, 0x02u, 0x03u };
    uint8_t f[70];
    f[0] = (uint8_t)PKT_SOF;
    size_t n = frame_build(f + 1, 0x10u, pl, 3u) + 1u;

    uint32_t t = 0u;
    packet_t got;
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);
    assert(got.cmd == 0x10u && got.len == 3u);
    assert(p.err_len == 1u);       /* 두 번째 A5가 LEN으로 해석되어 1회 */
    assert(p.err_crc == 0u);
    printf("  ok  resync: stray SOF before frame costs err_len=1, packet kept\n");
}

static void test_garbage_then_valid(void)
{
    parser_t p;
    parser_init(&p);

    /* SOF가 전혀 없는 쓰레기 */
    const uint8_t junk[6] = { 0x00u, 0xFFu, 0x7Eu, 0x12u, 0x34u, 0x99u };
    uint32_t t = 0u;
    assert(feed_all(&p, junk, sizeof junk, &t, 1u, NULL) == 0u);
    assert(p.st == ST_SOF);
    assert(p.err_crc == 0u && p.err_len == 0u && p.err_timeout == 0u);

    const uint8_t pl[2] = { 0xAAu, 0xBBu };
    uint8_t f[69];
    packet_t got;
    size_t n = frame_build(f, 0x55u, pl, 2u);
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);
    assert(got.cmd == 0x55u && got.payload[0] == 0xAAu && got.payload[1] == 0xBBu);
    /* SOF 없는 쓰레기는 에러 카운터를 전혀 올리지 않는다 */
    assert(p.err_crc == 0u && p.err_len == 0u && p.err_timeout == 0u);
    printf("  ok  garbage without SOF: silently dropped, next frame parses\n");
}

static void test_truncated_then_valid_no_timeout(void)
{
    /* 타임아웃이 없다면(=바이트가 쉬지 않고 이어지면) 잘린 프레임이 다음
     * 프레임을 먹어버린다. 이것이 타임아웃이 필요한 이유다. */
    parser_t p;
    parser_init(&p);

    const uint8_t pl[3] = { 0x01u, 0x02u, 0x03u };
    uint8_t f[69];
    size_t n = frame_build(f, 0x10u, pl, 3u);

    uint32_t t = 0u;
    /* 앞 4바이트만 보내고 끊긴 프레임 (A5 03 10 01) */
    assert(feed_all(&p, f, 4u, &t, 1u, NULL) == 0u);
    assert(p.st == ST_PAYLOAD);

    /* 곧바로(간격 1 ms) 온전한 프레임을 이어 붙인다 */
    assert(feed_all(&p, f, n, &t, 1u, NULL) == 0u);   /* 하나도 못 받는다 */
    assert(p.err_crc == 1u);       /* 잘린 프레임 + 앞부분이 CRC 실패 */
    assert(p.err_timeout == 0u);
    printf("  ok  truncated+valid without timeout: packet LOST (err_crc=1)\n");
}

static void test_truncated_then_valid_with_timeout(void)
{
    parser_t p;
    parser_init(&p);

    const uint8_t pl[3] = { 0x01u, 0x02u, 0x03u };
    uint8_t f[69];
    size_t n = frame_build(f, 0x10u, pl, 3u);

    uint32_t t = 0u;
    assert(feed_all(&p, f, 4u, &t, 1u, NULL) == 0u);
    assert(p.st == ST_PAYLOAD);

    /* 송신 측이 죽어서 200 ms 침묵한 뒤 새 프레임을 보낸다 */
    t += 200u;
    packet_t got;
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);
    assert(got.cmd == 0x10u && got.len == 3u);
    assert(p.err_timeout == 1u);
    assert(p.err_crc == 0u);
    printf("  ok  truncated+valid with 200 ms gap: err_timeout=1, packet OK\n");
}

static void test_timeout_boundary(void)
{
    parser_t p;
    const uint8_t pl[1] = { 0x42u };
    uint8_t f[69];
    size_t n = frame_build(f, 0x01u, pl, 1u);
    packet_t got;

    /* 정확히 PKT_TIMEOUT_MS 간격은 아직 타임아웃이 아니다(> 비교) */
    parser_init(&p);
    uint32_t t = 0u;
    assert(feed_all(&p, f, n, &t, PKT_TIMEOUT_MS, &got) == 1u);
    assert(p.err_timeout == 0u);

    /* 1 ms 더 벌리면 프레임마다 타임아웃이 걸려 하나도 못 받는다 */
    parser_init(&p);
    t = 0u;
    assert(feed_all(&p, f, n, &t, PKT_TIMEOUT_MS + 1u, NULL) == 0u);
    assert(p.err_timeout > 0u);
    printf("  ok  timeout boundary: ==50 ms ok, 51 ms drops the frame\n");
}

static void test_timeout_only_inside_frame(void)
{
    /* ST_SOF(프레임 밖)에서는 아무리 오래 쉬어도 타임아웃이 아니다.
     * idle 링크에서 err_timeout이 계속 올라가면 안 된다. */
    parser_t p;
    parser_init(&p);

    uint32_t t = 0u;
    for (int i = 0; i < 10; i++) {
        (void)parser_feed(&p, 0x00u, t);
        t += 10000u;
    }
    assert(p.err_timeout == 0u);

    const uint8_t pl[1] = { 0x01u };
    uint8_t f[69];
    packet_t got;
    size_t n = frame_build(f, 0x09u, pl, 1u);
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);
    assert(p.err_timeout == 0u);
    printf("  ok  idle link: long gaps outside a frame are not timeouts\n");
}

static void test_sof_inside_payload(void)
{
    /* payload에 0xA5가 있어도 길이 기반 파싱이라 문제없다 */
    parser_t p;
    parser_init(&p);

    const uint8_t pl[4] = { 0xA5u, 0xA5u, 0x00u, 0xA5u };
    uint8_t f[69];
    packet_t got;
    size_t n = frame_build(f, 0xA5u, pl, 4u);   /* CMD도 0xA5로 */

    uint32_t t = 0u;
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);
    assert(got.cmd == 0xA5u && got.len == 4u);
    assert(memcmp(got.payload, pl, 4u) == 0);
    assert(p.err_crc == 0u && p.err_len == 0u);
    printf("  ok  0xA5 inside CMD/payload: length-based parse unaffected\n");
}

static void test_back_to_back_frames(void)
{
    parser_t p;
    parser_init(&p);

    uint8_t f[69];
    uint32_t t = 0u;
    unsigned total = 0u;
    packet_t got;

    for (unsigned k = 0u; k < 16u; k++) {
        uint8_t pl[8];
        for (unsigned i = 0u; i < 8u; i++) {
            pl[i] = (uint8_t)(k * 8u + i);
        }
        size_t n = frame_build(f, (uint8_t)(0xC0u + k), pl, 8u);
        total += feed_all(&p, f, n, &t, 1u, &got);
        assert(got.cmd == (uint8_t)(0xC0u + k));
        assert(got.payload[7] == (uint8_t)(k * 8u + 7u));
    }
    assert(total == 16u);
    assert(p.err_crc == 0u && p.err_len == 0u && p.err_timeout == 0u);
    printf("  ok  16 back-to-back frames, no gap needed between them\n");
}

static void test_tick_wraparound(void)
{
    /* tick이 2^32에서 wrap하는 순간에도 unsigned 뺄셈이 올바르다 */
    parser_t p;
    parser_init(&p);

    const uint8_t pl[2] = { 0x11u, 0x22u };
    uint8_t f[69];
    packet_t got;
    size_t n = frame_build(f, 0x7Fu, pl, 2u);

    uint32_t t = 0xFFFFFFFCu;        /* 프레임 도중에 wrap */
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);
    assert(got.cmd == 0x7Fu);
    assert(p.err_timeout == 0u);
    printf("  ok  tick wraparound at 2^32 does not fake a timeout\n");
}

static void test_counters_are_independent(void)
{
    parser_t p;
    parser_init(&p);

    uint8_t f[69];
    const uint8_t pl[2] = { 0x01u, 0x02u };
    uint32_t t = 0u;

    /* 1) LEN 초과 */
    const uint8_t bad_len[2] = { (uint8_t)PKT_SOF, 200u };
    (void)feed_all(&p, bad_len, 2u, &t, 1u, NULL);

    /* 2) CRC 불일치 */
    size_t n = frame_build(f, 0x01u, pl, 2u);
    f[n - 1u] ^= 0x01u;
    (void)feed_all(&p, f, n, &t, 1u, NULL);

    /* 3) 타임아웃 */
    n = frame_build(f, 0x02u, pl, 2u);
    (void)feed_all(&p, f, 3u, &t, 1u, NULL);
    t += 100u;
    packet_t got;
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);

    assert(p.err_len == 1u);
    assert(p.err_crc == 1u);
    assert(p.err_timeout == 1u);
    printf("  ok  err_len / err_crc / err_timeout counted separately (1/1/1)\n");
}

static void test_no_state_leak_between_frames(void)
{
    /* 긴 프레임 다음의 짧은 프레임이 이전 payload 잔재에 영향받지 않아야 한다 */
    parser_t p;
    parser_init(&p);

    uint8_t big[PKT_MAX_PAYLOAD];
    memset(big, 0xEEu, sizeof big);
    uint8_t f[69];
    packet_t got;
    uint32_t t = 0u;

    size_t n = frame_build(f, 0x01u, big, (uint8_t)PKT_MAX_PAYLOAD);
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);

    const uint8_t small[1] = { 0x5Au };
    n = frame_build(f, 0x02u, small, 1u);
    assert(feed_all(&p, f, n, &t, 1u, &got) == 1u);
    assert(got.len == 1u && got.payload[0] == 0x5Au);
    printf("  ok  short frame after a 64-byte frame reports len=1 correctly\n");
}

int main(void)
{
    printf("05_uart_parser (solution)\n");
    test_crc_check_value();
    test_happy_path();
    test_zero_length_and_max_length();
    test_bad_crc();
    test_oversize_length();
    test_resync_sof_in_len_slot();
    test_garbage_then_valid();
    test_truncated_then_valid_no_timeout();
    test_truncated_then_valid_with_timeout();
    test_timeout_boundary();
    test_timeout_only_inside_frame();
    test_sof_inside_payload();
    test_back_to_back_frames();
    test_tick_wraparound();
    test_counters_are_independent();
    test_no_state_leak_between_frames();
    printf("ALL TESTS PASSED\n");
    return 0;
}
