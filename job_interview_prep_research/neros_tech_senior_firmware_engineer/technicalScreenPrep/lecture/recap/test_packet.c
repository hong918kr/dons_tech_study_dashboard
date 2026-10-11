/* Task 2 테스트 — 패킷 추출: 정상 · 쪼개져 도착 · 앞쪽 쓰레기 · CRC 오류 · 연속 · 작은 버퍼 · 가짜 긴 LEN */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "crc.h"
#include "packet.h"

static size_t frame(uint8_t *dst, const uint8_t *payload, uint8_t len)   /* 테스트용 프레임 생성기 */
{
    dst[0] = PKT_SYNC;
    dst[1] = len;
    memcpy(&dst[2], payload, len);
    dst[2 + len] = crc8(&dst[1], (size_t)len + 1);
    return (size_t)len + 3;
}

// --- tests ---
static uint8_t store[64];
static rb_t rb;
static uint8_t out[16];

static void reset(void) { rb_init(&rb, store, sizeof store); }

static void test_crc_known_vector(void)
{
    assert(crc8((const uint8_t *)"123456789", 9) == 0xF4);      /* 라이브러리부터 검증 */
}

static void test_one_packet(void)
{
    uint8_t f[16];
    const uint8_t p[] = {1, 2, 3};
    reset();
    rb_push(&rb, f, frame(f, p, 3));
    assert(extract_packet(&rb, out, sizeof out) == 3 && memcmp(out, p, 3) == 0);
    assert(rb_is_empty(&rb));
}

static void test_split_arrival(void)
{
    uint8_t f[16];
    const uint8_t p[] = {7, 8};
    size_t n = frame(f, p, 2);
    reset();
    rb_push(&rb, f, 3);                                         /* 반만 도착 */
    assert(extract_packet(&rb, out, sizeof out) == PKT_NEED_MORE && rb.count == 3);   /* 버리지 않고 기다림 */
    rb_push(&rb, f + 3, n - 3);
    assert(extract_packet(&rb, out, sizeof out) == 2 && out[1] == 8);
}

static void test_garbage_then_packet(void)
{
    uint8_t f[16];
    const uint8_t junk[] = {0x00, 0x13, 0x37}, p[] = {5};
    reset();
    rb_push(&rb, junk, 3);
    rb_push(&rb, f, frame(f, p, 1));
    assert(extract_packet(&rb, out, sizeof out) == 1 && out[0] == 5);
}

static void test_bad_crc_skipped_next_found(void)
{
    uint8_t f[16], g[16];
    const uint8_t p[] = {1, 2}, q[] = {9};
    size_t n = frame(f, p, 2);
    f[n - 1] ^= 0xFF;                                           /* CRC 깨뜨리기 */
    reset();
    rb_push(&rb, f, n);
    rb_push(&rb, g, frame(g, q, 1));
    assert(extract_packet(&rb, out, sizeof out) == 1 && out[0] == 9);
}

static void test_two_packets_back_to_back_and_empty_payload(void)
{
    uint8_t f[16], g[16];
    const uint8_t p[] = {4};
    reset();
    rb_push(&rb, f, frame(f, p, 1));
    rb_push(&rb, g, frame(g, p, 0));                            /* 길이 0 payload */
    assert(extract_packet(&rb, out, sizeof out) == 1);
    assert(extract_packet(&rb, out, sizeof out) == 0);
    assert(extract_packet(&rb, out, sizeof out) == PKT_NEED_MORE);
}

static void test_out_buffer_too_small(void)
{
    uint8_t f[16];
    const uint8_t p[] = {1, 2, 3, 4};
    reset();
    rb_push(&rb, f, frame(f, p, 4));
    assert(extract_packet(&rb, out, 2) == PKT_TOO_BIG && rb_is_empty(&rb));   /* 버리고 다음으로 */
}

static void test_fake_sync_with_impossible_length(void)
{
    uint8_t small[8], f[8];
    const uint8_t fake[] = {PKT_SYNC, 200}, p[] = {6};          /* 버퍼(8)에 절대 못 들어올 길이 */
    rb_init(&rb, small, sizeof small);
    rb_push(&rb, fake, 2);
    rb_push(&rb, f, frame(f, p, 1));
    assert(extract_packet(&rb, out, sizeof out) == 1 && out[0] == 6);     /* 데드락 없이 회복 */
}

int main(void)
{
    test_crc_known_vector();
    test_one_packet();
    test_split_arrival();
    test_garbage_then_packet();
    test_bad_crc_skipped_next_found();
    test_two_packets_back_to_back_and_empty_payload();
    test_out_buffer_too_small();
    test_fake_sync_with_impossible_length();
    puts("test_packet: PASS");
    return 0;
}
// --- end ---
