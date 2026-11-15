/* TOOL-B11 AddressSanitizer로 메모리 버그 잡기 — packet buffer module
 *
 * 이 모듈에는 메모리 버그가 6개 숨어 있다 (heap / stack / global overflow,
 * use-after-free, double-free, leak). ASan 리포트를 읽고 TODO 줄만 고쳐라.
 * 빌드/검사: bash check.sh starter   (driver.c 가 케이스별로 한 프로세스씩 실행)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"

uint16_t crc16_ccitt(const uint8_t *d, size_t n)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)(d[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

pkt_t *pkt_new(uint8_t kind, const uint8_t *payload, size_t n)
{
    if (kind >= PKT_NKINDS || n > PKT_MAX_PAYLOAD)
        return NULL;
    pkt_t *p = calloc(1, sizeof *p);
    if (p == NULL)
        return NULL;
    p->frame = malloc(PKT_HDR_LEN + n + PKT_CRC_LEN);
    if (p->frame == NULL) {
        free(p);
        return NULL;
    }
    p->kind = kind;
    p->len = n;
    p->frame[0] = PKT_SYNC;
    p->frame[1] = kind;
    p->frame[2] = (uint8_t)(n & 0xFFu);
    p->frame[3] = (uint8_t)(n >> 8);
    if (n != 0)
        memcpy(p->frame + PKT_HDR_LEN, payload, n);
    uint16_t crc = crc16_ccitt(p->frame, PKT_HDR_LEN + n);
    p->frame[PKT_HDR_LEN + n] = (uint8_t)(crc & 0xFFu);
    p->frame[PKT_HDR_LEN + n + 1] = (uint8_t)(crc >> 8);
    return p;
}

void pkt_free(pkt_t *p)
{
    if (p == NULL)
        return;
    free(p->frame);
    free(p);
}

size_t pkt_frame_len(const pkt_t *p)
{
    return PKT_HDR_LEN + p->len + PKT_CRC_LEN;
}

/* ---------------- Part A — overflows ---------------- */

/* 텍스트 payload (예: "TEMP=23.5C") 를 로그용 C 문자열로 복사한다. */
char *pkt_payload_str(const pkt_t *p)
{
    char *s = malloc(p->len);                       /* TODO(heap): '\0' 자리 */
    if (s == NULL)
        return NULL;
    memcpy(s, p->frame + PKT_HDR_LEN, p->len);
    s[p->len] = '\0';
    return s;
}

/* 프레임 앞 8바이트를 "A5020500..." 로 (디버그 로그 한 줄). */
void pkt_hex_prefix(const pkt_t *p, char *out, size_t outsz)
{
    static const char hx[] = "0123456789ABCDEF";
    char line[2 * PKT_HEX_BYTES];                   /* TODO(stack): 크기 */
    size_t n = pkt_frame_len(p) < PKT_HEX_BYTES ? pkt_frame_len(p) : PKT_HEX_BYTES;
    size_t i;
    for (i = 0; i < n; i++) {
        line[2 * i] = hx[p->frame[i] >> 4];
        line[2 * i + 1] = hx[p->frame[i] & 0xFu];
    }
    line[2 * i] = '\0';
    snprintf(out, outsz, "%s", line);
}

/* 수신 바이트의 kind 별 통계. kind 는 선로에서 온 값이라 믿을 수 없다. */
static uint32_t g_kind_count[PKT_NKINDS];

int pkt_count_kind(unsigned kind)
{
    if (kind > PKT_NKINDS)                          /* TODO(global): 경계 */
        return -1;
    return (int)++g_kind_count[kind];
}

uint32_t pkt_kind_total(unsigned kind)
{
    return kind < PKT_NKINDS ? g_kind_count[kind] : 0u;
}

/* ---------------- Part B — lifetime ---------------- */

void txq_push(txq_t *q, pkt_t *p)
{
    p->next = NULL;
    if (q->tail != NULL)
        q->tail->next = p;
    else
        q->head = p;
    q->tail = p;
    q->count++;
}

/* 큐의 모든 패킷을 "송신" (바이트 수만 센다) 하고 해제한다. */
size_t txq_drain(txq_t *q)
{
    size_t bytes = 0;
    for (pkt_t *p = q->head; p != NULL; p = p->next) {   /* TODO(uaf) */
        bytes += pkt_frame_len(p);
        pkt_free(p);
    }
    q->head = q->tail = NULL;
    q->count = 0;
    return bytes;
}

/* 프레임 저장소만 버리고 구조체는 재사용 (pkt_free 는 나중에 따로 부른다). */
void pkt_reset(pkt_t *p)
{
    free(p->frame);
    p->len = 0;                                     /* TODO(double-free) */
}

/* 수신 프레임을 검사해 pkt 로 만든다. 잘못된 프레임이면 NULL. */
pkt_t *pkt_from_wire(const uint8_t *raw, size_t n)
{
    if (n < PKT_HDR_LEN + PKT_CRC_LEN || raw[0] != PKT_SYNC)
        return NULL;
    size_t len = (size_t)raw[2] | ((size_t)raw[3] << 8);
    if (PKT_HDR_LEN + len + PKT_CRC_LEN != n)
        return NULL;
    pkt_t *p = pkt_new(raw[1], raw + PKT_HDR_LEN, len);
    if (p == NULL)
        return NULL;
    if (memcmp(p->frame + PKT_HDR_LEN + len, raw + PKT_HDR_LEN + len, PKT_CRC_LEN) != 0)
        return NULL;                                /* TODO(leak) */
    return p;
}
