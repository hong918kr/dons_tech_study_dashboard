/* 05_uart_parser.c — byte-at-a-time UART 프레임 파서
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 05_uart_parser.c -o run_05
 * 또는: make prob N=05 && make run N=05
 *
 * 요구사항: 문제 파일(05_uart_parser.md)을 읽고, 아래 // TODO 섹션들을 채워라.
 *
 * 프로토콜:
 *   | SOF(0xA5) | LEN | CMD | PAYLOAD[LEN] | CRC_H | CRC_L |
 *
 * - CRC-16/CCITT-FALSE (poly 0x1021)
 * - timeout: 바이트 간격이 50ms 초과하면 프레임 폐기
 * - state machine: SOF -> LEN -> CMD -> PAYLOAD -> CRC_H -> CRC_L
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================================================================== */
/* 상수                                                              */
/* ================================================================== */

#define PKT_SOF          0xA5u
#define PKT_MAX_PAYLOAD  64u
#define PKT_TIMEOUT_MS   50u

/* ================================================================== */
/* 타입 — TODO: 구조체 정의                                            */
/* ================================================================== */

typedef enum {
    // TODO: 다음 상태들을 추가하세요:
    // ST_SOF,      /* SOF 찾는 중 */
    // ST_LEN,      /* LEN 바이트 대기 */
    // ST_CMD,      /* CMD 바이트 대기 */
    // ST_PAYLOAD,  /* payload 수집 중 */
    // ST_CRC_H,    /* CRC 상위 바이트 */
    // ST_CRC_L     /* CRC 하위 바이트 */
} pstate_t;

typedef struct {
    // TODO: 다음 필드들을 추가하세요:
    // uint8_t cmd;
    // uint8_t len;
    // uint8_t payload[PKT_MAX_PAYLOAD];
} packet_t;

typedef struct {
    // TODO: 다음 필드들을 추가하세요:
    // pstate_t st;            /* 현재 상태 */
    // packet_t pkt;           /* 현재 구성 중인 패킷 */
    // uint8_t  pos;           /* payload에 지금까지 담은 개수 */
    // uint16_t crc;           /* 누적 계산한 CRC */
    // uint16_t rx_crc;        /* 프레임에 실려 온 CRC */
    // uint32_t last_ms;       /* 직전 바이트 tick */
    // uint32_t err_crc;       /* CRC 불일치 횟수 */
    // uint32_t err_len;       /* LEN 초과 횟수 */
    // uint32_t err_timeout;   /* timeout 횟수 */
} parser_t;

/* ================================================================== */
/* CRC-16/CCITT-FALSE — TODO: 구현                                    */
/* ================================================================== */

static uint16_t crc16_byte(uint16_t crc, uint8_t b)
{
    // TODO: CRC-16/CCITT-FALSE를 계산하세요.
    // 1. crc ^= (uint16_t)((uint16_t)b << 8)  (바이트를 상위에 XOR)
    // 2. for (i = 0; i < 8; i++) {
    //      if (crc & 0x8000) {
    //          crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
    //      } else {
    //          crc = (uint16_t)(crc << 1);
    //      }
    //    }
    // 3. return crc;
}

/* ================================================================== */
/* 파서 초기화 — TODO: 구현                                             */
/* ================================================================== */

void parser_init(parser_t *p)
{
    // TODO: 구현하세요.
    // p->st = ST_SOF;
    // p->crc = 0xFFFFu;  /* CRC 초기값 */
    // p->pos = 0u;
    // p->last_ms = 0u;
    // p->err_crc = p->err_len = p->err_timeout = 0u;
    // memset(&p->pkt, 0, sizeof p->pkt);
}

/* ================================================================== */
/* 파서 feed — TODO: 구현                                              */
/* ================================================================== */

/* 한 바이트를 파서에 먹인다.
 * 완성된 패킷이 있으면 true 반환, 결과는 p->pkt에 저장.
 * now_ms: 현재 millisecond tick (테스트용 가짜 값 가능)
 */
bool parser_feed(parser_t *p, uint8_t byte, uint32_t now_ms)
{
    // TODO: 상태 머신을 구현하세요. 각 상태에서:
    //
    // ST_SOF:
    //   - byte == PKT_SOF? 상태를 ST_LEN으로, CRC 초기화, last_ms 갱신.
    //   - 아니면: ST_SOF 유지.
    //
    // ST_LEN:
    //   - timeout 체크: now_ms - last_ms > PKT_TIMEOUT_MS? err_timeout++, ST_SOF로 리셋.
    //   - byte > PKT_MAX_PAYLOAD? err_len++, ST_SOF로 리셋.
    //   - 아니면: pkt.len = byte, crc = crc16_byte(crc, byte), ST_CMD로.
    //   - last_ms = now_ms.
    //
    // ST_CMD, ST_PAYLOAD, ST_CRC_H:
    //   - timeout 체크 후, 바이트 처리, CRC 누적, 상태 전이.
    //
    // ST_CRC_L (마지막):
    //   - rx_crc 하위 바이트 받음.
    //   - CRC 검사: (crc == rx_crc)? true 반환, ST_SOF로 리셋.
    //   - 아니면: err_crc++, ST_SOF로 리셋, false 반환.
    //
    // 반환값: 패킷 완성 여부 (ST_CRC_L에서만 true 가능)
}

/* ================================================================== */
/* 관찰 함수 — TODO: 구현                                              */
/* ================================================================== */

uint32_t parser_error_crc(const parser_t *p)
{
    // TODO: return p->err_crc;
}

uint32_t parser_error_len(const parser_t *p)
{
    // TODO: return p->err_len;
}

uint32_t parser_error_timeout(const parser_t *p)
{
    // TODO: return p->err_timeout;
}

/* ================================================================== */
/* 테스트                                                             */
/* ================================================================== */

#define OK(msg) printf("  ok  %s\n", (msg))

static void test_crc16_byte(void)
{
    uint16_t crc = 0xFFFFu;
    crc = crc16_byte(crc, 0x00);
    crc = crc16_byte(crc, 0x05);  /* LEN=5 */
    crc = crc16_byte(crc, 0x01);  /* CMD=1 */
    crc = crc16_byte(crc, 0x41);  /* 'A' */
    crc = crc16_byte(crc, 0x42);  /* 'B' */
    crc = crc16_byte(crc, 0x43);  /* 'C' */
    crc = crc16_byte(crc, 0x44);  /* 'D' */
    crc = crc16_byte(crc, 0x45);  /* 'E' */

    /* 정답: CRC-16/CCITT-FALSE("LEN CMD ABCDE") = 0xE2F0 */
    assert(crc == 0xE2F0u);
    OK("CRC-16/CCITT-FALSE 계산");
}

static void test_valid_packet(void)
{
    parser_t parser;
    parser_init(&parser);

    /* 완성된 프레임:
     * | 0xA5 | 5 | 1 | A B C D E | 0xE2 | 0xF0 |
     */
    uint32_t tick = 0;

    assert(!parser_feed(&parser, 0xA5, tick++));  /* SOF */
    assert(!parser_feed(&parser, 0x05, tick++));  /* LEN */
    assert(!parser_feed(&parser, 0x01, tick++));  /* CMD */
    assert(!parser_feed(&parser, 0x41, tick++));  /* 'A' */
    assert(!parser_feed(&parser, 0x42, tick++));  /* 'B' */
    assert(!parser_feed(&parser, 0x43, tick++));  /* 'C' */
    assert(!parser_feed(&parser, 0x44, tick++));  /* 'D' */
    assert(!parser_feed(&parser, 0x45, tick++));  /* 'E' */
    assert(!parser_feed(&parser, 0xE2, tick++));  /* CRC_H */
    assert(parser_feed(&parser, 0xF0, tick++));   /* CRC_L -> complete */

    assert(parser.pkt.cmd == 1);
    assert(parser.pkt.len == 5);
    assert(parser.pkt.payload[0] == 0x41);
    assert(parser.pkt.payload[4] == 0x45);
    assert(parser_error_crc(&parser) == 0);

    OK("유효한 패킷 수신 및 완성");
}

static void test_sof_resync(void)
{
    parser_t parser;
    parser_init(&parser);

    uint32_t tick = 0;

    /* 쓰레기, 그 다음 유효한 프레임 */
    assert(!parser_feed(&parser, 0xFF, tick++));  /* noise */
    assert(!parser_feed(&parser, 0x00, tick++));  /* noise */
    assert(!parser_feed(&parser, 0xA5, tick++));  /* SOF 찾음 */
    assert(!parser_feed(&parser, 0x02, tick++));  /* LEN */
    assert(!parser_feed(&parser, 0x42, tick++));  /* CMD */
    assert(!parser_feed(&parser, 0xAA, tick++));  /* payload[0] */
    assert(!parser_feed(&parser, 0xBB, tick++));  /* payload[1] */

    /* CRC(LEN=2, CMD=0x42, PAYLOAD=[0xAA, 0xBB]) 계산 */
    uint16_t crc = 0xFFFFu;
    crc = crc16_byte(crc, 0x02);
    crc = crc16_byte(crc, 0x42);
    crc = crc16_byte(crc, 0xAA);
    crc = crc16_byte(crc, 0xBB);

    assert(!parser_feed(&parser, (crc >> 8) & 0xFF, tick++));  /* CRC_H */
    assert(parser_feed(&parser, crc & 0xFF, tick++));          /* CRC_L */

    OK("쓰레기 이후 재동기화");
}

static void test_crc_mismatch(void)
{
    parser_t parser;
    parser_init(&parser);

    uint32_t tick = 0;

    assert(!parser_feed(&parser, 0xA5, tick++));  /* SOF */
    assert(!parser_feed(&parser, 0x01, tick++));  /* LEN */
    assert(!parser_feed(&parser, 0x99, tick++));  /* CMD */
    assert(!parser_feed(&parser, 0x88, tick++));  /* payload */
    assert(!parser_feed(&parser, 0xFF, tick++));  /* CRC_H (잘못됨) */
    assert(!parser_feed(&parser, 0xFF, tick++));  /* CRC_L */

    assert(parser_error_crc(&parser) == 1);
    OK("CRC 불일치 감지");
}

static void test_length_overflow(void)
{
    parser_t parser;
    parser_init(&parser);

    uint32_t tick = 0;

    assert(!parser_feed(&parser, 0xA5, tick++));      /* SOF */
    assert(!parser_feed(&parser, 0x41, tick++));      /* LEN=65 (> MAX=64) */

    assert(parser_error_len(&parser) == 1);
    /* 상태가 ST_SOF로 돌아가서 다음 SOF를 찾는다 */

    OK("LEN 초과 감지");
}

static void test_timeout(void)
{
    parser_t parser;
    parser_init(&parser);

    uint32_t tick = 0;

    assert(!parser_feed(&parser, 0xA5, tick));       /* SOF at tick=0 */
    assert(!parser_feed(&parser, 0x02, tick + 10));  /* LEN at tick=10 (OK) */
    /* tick이 tick+10+51 = tick+61로 60ms 이상 진행 = timeout */
    assert(!parser_feed(&parser, 0xA5, tick + 100)); /* timeout 감지, SOF 찾음 */

    assert(parser_error_timeout(&parser) == 1);
    OK("inter-byte timeout 감지");
}

int main(void)
{
    printf("=== 05 UART 프레임 파서 ===\n");
    test_crc16_byte();
    test_valid_packet();
    test_sof_resync();
    test_crc_mismatch();
    test_length_overflow();
    test_timeout();
    printf("ALL TESTS PASSED\n");
    return 0;
}
