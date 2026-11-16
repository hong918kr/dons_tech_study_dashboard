/* OS-A13 추가 연습 — 한 번에 검증
 *   1. UART 위 RPC 프레이밍: COBS + CRC-16 은 바이트 하나가 깨져도 다음 0x00 에서 다시 맞춘다 (길이 prefix 와 비교)
 *   2. protobuf 식 varint/wire-type TLV: 모르는 필드를 wire type 으로 건너뛴다
 *   3. 스트림 흐름 제어: 크레딧(window) 기반 — 받는 쪽 버퍼가 W 를 넘지 않는다
 *   4. 재시도 + 중복 제거 표 → at-most-once 실행 (부작용 정확히 1 번)
 *   5. 마감 전파 (deadline propagation): 남은 예산으로 하위 호출 마감 정하기
 *   6. req_id 할당기: 0 과 사용 중인 id 를 건너뛰고, 꽉 차면 무한 루프 대신 실패
 * 전부 단일 스레드, 고정 시드 → 결정적.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static uint32_t g_rng = 0xA13u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* ===================================================================== 1. COBS + CRC-16 */
static uint16_t crc16_ccitt_false(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}
/* 블록 = [코드 k][0 아닌 바이트 k-1 개]; k < 0xFF 면 블록 뒤에 0 하나. 출력에 0x00 이 없다. */
static size_t cobs_encode(const uint8_t *in, size_t n, uint8_t *out) {
    size_t o = 1, code_at = 0;
    uint8_t code = 1;
    for (size_t i = 0; i < n; i++) {
        if (in[i] == 0) { out[code_at] = code; code_at = o++; code = 1; continue; }
        out[o++] = in[i];
        if (++code == 0xFF) { out[code_at] = code; code_at = o++; code = 1; }
    }
    out[code_at] = code;
    return o;
}
static int cobs_decode(const uint8_t *in, size_t n, uint8_t *out) {
    size_t i = 0, o = 0;
    while (i < n) {
        uint8_t code = in[i++];
        if (code == 0 || i + code - 1 > n) return -1;
        for (int k = 1; k < code; k++) { if (in[i] == 0) return -1; out[o++] = in[i++]; }
        if (code != 0xFF && i < n) out[o++] = 0;
    }
    return (int)o;
}
/* 링크 프레임 = COBS(payload ‖ crc_hi ‖ crc_lo) ‖ 0x00 */
static size_t link_frame(const uint8_t *pl, size_t n, uint8_t *out) {
    uint8_t tmp[300];
    memcpy(tmp, pl, n);
    uint16_t c = crc16_ccitt_false(pl, n);
    tmp[n] = (uint8_t)(c >> 8);
    tmp[n + 1] = (uint8_t)c;
    size_t e = cobs_encode(tmp, n + 2, out);
    out[e] = 0x00;
    return e + 1;
}
typedef struct { uint8_t buf[400]; size_t n; int good, bad; uint32_t ids[300]; } link_rx_t;
static void link_rx_byte(link_rx_t *rx, uint8_t b) {          /* UART RX ISR 에서 바이트마다 */
    if (b != 0x00) { if (rx->n < sizeof rx->buf) rx->buf[rx->n++] = b; else rx->n = sizeof rx->buf + 1; return; }
    uint8_t dec[400];
    int d = (rx->n > 0 && rx->n <= sizeof rx->buf) ? cobs_decode(rx->buf, rx->n, dec) : -1;
    if (d >= 6 && crc16_ccitt_false(dec, (size_t)d - 2) == (uint16_t)(dec[d - 2] << 8 | dec[d - 1])) {
        if (rx->good < 300) rx->ids[rx->good] = (uint32_t)dec[0] | (uint32_t)dec[1] << 8 | (uint32_t)dec[2] << 16 | (uint32_t)dec[3] << 24;
        rx->good++;
    } else if (rx->n > 0) rx->bad++;
    rx->n = 0;                                                /* 0x00 = 무조건 새 프레임의 시작 → 재동기화 */
}

static void ex1_cobs(void) {
    printf("--- 연습 1: COBS + CRC 재동기화 ---\n");
    uint8_t out[16];
    CHECK(crc16_ccitt_false((const uint8_t *)"123456789", 9) == 0x29B1);
    CHECK(cobs_encode((const uint8_t[]){ 0x11, 0x22, 0x00, 0x33 }, 4, out) == 5 &&
          memcmp(out, (const uint8_t[]){ 0x03, 0x11, 0x22, 0x02, 0x33 }, 5) == 0);
    CHECK(cobs_encode((const uint8_t[]){ 0x00 }, 1, out) == 2 && out[0] == 1 && out[1] == 1);

    /* 40 프레임을 이어 보내되 10 번째 프레임 가운데 바이트 하나를 깨뜨린다 */
    enum { N = 40, BAD = 10 };
    static uint8_t wire[N * 300], wire_lp[N * 300];
    size_t wn = 0, ln = 0, bad_at = 0, bad_at_lp = 0;
    for (uint32_t i = 0; i < N; i++) {
        uint8_t pl[200];
        size_t n = 8 + xs32() % 150;
        for (size_t k = 0; k < n; k++) pl[k] = (uint8_t)(xs32() % 4 ? xs32() : 0);   /* 0 이 섞인 데이터 */
        pl[0] = (uint8_t)i; pl[1] = pl[2] = pl[3] = 0;
        if (i == BAD) bad_at = wn + 1 + n / 2;
        wn += link_frame(pl, n, wire + wn);
        /* 비교: 길이 prefix 프레임 [u16 len][payload][crc16] */
        if (i == BAD) bad_at_lp = ln;                         /* 길이 바이트 자체가 깨진 경우 */
        wire_lp[ln] = (uint8_t)n; wire_lp[ln + 1] = (uint8_t)(n >> 8);
        memcpy(wire_lp + ln + 2, pl, n);
        uint16_t c = crc16_ccitt_false(pl, n);
        wire_lp[ln + 2 + n] = (uint8_t)(c >> 8); wire_lp[ln + 3 + n] = (uint8_t)c;
        ln += 4 + n;
    }
    wire[bad_at] ^= 0x5A;
    wire_lp[bad_at_lp] ^= 0x5A;                               /* 길이 하위 바이트가 틀어진다 */
    link_rx_t rx;
    memset(&rx, 0, sizeof rx);
    for (size_t i = 0; i < wn; i++) link_rx_byte(&rx, wire[i]);
    int seq_ok = 1;
    for (int i = 0, want = 0; i < rx.good && i < 300; i++, want++) {
        if (want == BAD) want++;
        if (rx.ids[i] != (uint32_t)want) seq_ok = 0;
    }
    printf("      COBS: 받음 %d, 버림 %d (깨진 프레임 %d 번만 잃는다)\n", rx.good, rx.bad, BAD);
    CHECK(rx.good == N - 1 && rx.bad >= 1 && seq_ok);

    /* 길이 prefix: CRC 가 틀리면 버리지만 "다음 프레임이 어디서 시작하는지" 는 잃는다 */
    int lp_good = 0;
    for (size_t p = 0; p + 4 <= ln;) {
        size_t n = (size_t)wire_lp[p] | (size_t)wire_lp[p + 1] << 8;
        if (p + 4 + n > ln) break;
        uint16_t c = crc16_ccitt_false(wire_lp + p + 2, n);
        if (c == (uint16_t)(wire_lp[p + 2 + n] << 8 | wire_lp[p + 3 + n])) { lp_good++; p += 4 + n; }
        else p += 4 + n;                                      /* 틀린 길이만큼 건너뛴다 → 경계가 어긋난다 */
    }
    printf("      길이 prefix: 받음 %d / %d (깨진 길이 하나 뒤로 경계를 잃음)\n", lp_good, N);
    CHECK(lp_good <= BAD + 1);
}

/* ===================================================================== 2. protobuf 식 wire format */
static size_t pb_varint(uint8_t *o, uint64_t v) { size_t n = 0; do { uint8_t b = v & 0x7F; v >>= 7; o[n++] = (uint8_t)(b | (v ? 0x80 : 0)); } while (v); return n; }
static int pb_get_varint(const uint8_t *p, size_t n, size_t *off, uint64_t *v) {
    uint64_t r = 0;
    for (int i = 0; i < 10; i++) {
        if (*off >= n) return -1;
        uint8_t b = p[(*off)++];
        r |= (uint64_t)(b & 0x7F) << (7 * i);
        if (!(b & 0x80)) { *v = r; return 0; }
    }
    return -1;                                                /* 10 바이트 넘는 varint = 깨짐 */
}
static uint32_t zigzag32(int32_t v) { return ((uint32_t)v << 1) ^ (uint32_t)(v >> 31); }
typedef struct { uint64_t f1; char f2[32]; int unknown; } v1msg_t;
/* v1 디코더: 필드 1(varint), 2(string) 만 안다. 나머지는 wire type 으로 길이를 알아 건너뛴다. */
static int pb_decode_v1(const uint8_t *p, size_t n, v1msg_t *m) {
    memset(m, 0, sizeof *m);
    size_t off = 0;
    while (off < n) {
        uint64_t key, v;
        if (pb_get_varint(p, n, &off, &key)) return -1;
        uint32_t field = (uint32_t)(key >> 3), wt = (uint32_t)(key & 7);
        if (field == 0) return -1;
        switch (wt) {
        case 0: if (pb_get_varint(p, n, &off, &v)) return -1; if (field == 1) { m->f1 = v; continue; } break;
        case 1: if (n - off < 8) return -1; off += 8; break;
        case 5: if (n - off < 4) return -1; off += 4; break;
        case 2:
            if (pb_get_varint(p, n, &off, &v) || v > n - off) return -1;
            if (field == 2 && v < sizeof m->f2) { memcpy(m->f2, p + off, (size_t)v); off += (size_t)v; continue; }
            off += (size_t)v;
            break;
        default: return -1;                                   /* 3/4(group) 미지원, 6/7 은 없는 type */
        }
        m->unknown++;
    }
    return 0;
}
static void ex2_protobuf(void) {
    printf("--- 연습 2: varint TLV 와 모르는 필드 ---\n");
    uint8_t b[64];
    size_t n = pb_varint(b, (1 << 3) | 0);
    n += pb_varint(b + n, 150);
    CHECK(n == 3 && b[0] == 0x08 && b[1] == 0x96 && b[2] == 0x01);   /* protobuf 문서의 예 */
    CHECK(zigzag32(0) == 0 && zigzag32(-1) == 1 && zigzag32(1) == 2 && zigzag32(-2) == 3);
    b[n++] = (2 << 3) | 2; b[n++] = 7; memcpy(b + n, "testing", 7); n += 7;
    n += pb_varint(b + n, (15 << 3) | 0); n += pb_varint(b + n, 300);          /* v2 필드들 */
    n += pb_varint(b + n, (16 << 3) | 2); b[n++] = 3; memcpy(b + n, "new", 3); n += 3;
    n += pb_varint(b + n, (17 << 3) | 5); memset(b + n, 0xAB, 4); n += 4;
    n += pb_varint(b + n, (18 << 3) | 1); memset(b + n, 0xCD, 8); n += 8;
    v1msg_t m;
    CHECK(pb_decode_v1(b, n, &m) == 0 && m.f1 == 150 && strcmp(m.f2, "testing") == 0 && m.unknown == 4);
    CHECK(pb_decode_v1(b, n - 1, &m) == -1);                  /* 잘린 fixed64 */
    uint8_t longv[12] = { 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01 };
    CHECK(pb_decode_v1(longv, sizeof longv, &m) == -1);       /* 11 바이트 varint */
    uint8_t grp[2] = { (3 << 3) | 3, 0 };
    CHECK(pb_decode_v1(grp, 2, &m) == -1);
}

/* ===================================================================== 3. 크레딧 흐름 제어 */
typedef struct { int max_buf, updates, delivered; bool in_order; } fc_res_t;
static fc_res_t fc_sim(int N, int W, bool flow_control, uint32_t seed) {
    fc_res_t r = { 0, 0, 0, true };
    int credit = W, sent = 0, buf = 0, consumed_since = 0, next_expect = 0;
    uint32_t x = seed;
    while (r.delivered < N) {
        int burst = 4;                                        /* 보내는 쪽은 빠르다: 한 tick 에 최대 4 개 */
        while (burst-- && sent < N && (!flow_control || credit > 0)) { sent++; buf++; if (flow_control) credit--; }
        if (buf > r.max_buf) r.max_buf = buf;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int eat = (int)(x % 3);                               /* 받는 쪽은 느리다: 0~2 개 */
        while (eat-- && buf > 0) {
            buf--;
            if (r.delivered != next_expect) r.in_order = false;
            r.delivered++; next_expect++;
            consumed_since++;
        }
        if (flow_control && consumed_since >= W / 2) {        /* WINDOW_UPDATE: 소비한 만큼 돌려준다 */
            credit += consumed_since; consumed_since = 0; r.updates++;
        }
        if (flow_control && sent == N && consumed_since > 0 && buf == 0) { credit += consumed_since; consumed_since = 0; }
    }
    return r;
}
static void ex3_flow(void) {
    printf("--- 연습 3: 크레딧 흐름 제어 ---\n");
    fc_res_t a = fc_sim(1000, 16, true, 7), b = fc_sim(1000, 16, false, 7);
    printf("      window 16: 받는 쪽 최대 버퍼 %d, WINDOW_UPDATE %d 번 / 흐름 제어 없음: 최대 버퍼 %d\n",
           a.max_buf, a.updates, b.max_buf);
    CHECK(a.delivered == 1000 && a.in_order && a.max_buf <= 16);
    CHECK(b.max_buf > 16);
    CHECK(a.updates >= 1000 / 16 && a.updates <= 1000 / 8 + 1);
}

/* ===================================================================== 4. 재시도 + 중복 제거 */
#define DEDUP_MAX 32
typedef struct { bool used; uint32_t client, req_id; int64_t resp; } dedup_t;
typedef struct { dedup_t t[DEDUP_MAX]; int64_t balance; int executions, hits, size, max_size; } bank_t;
static int64_t bank_deposit(bank_t *b, bool dedup, uint32_t client, uint32_t req_id, uint32_t ack_upto, int64_t amt) {
    if (dedup) {
        for (int i = 0; i < DEDUP_MAX; i++)                  /* 클라이언트가 "여기까지 받았다" 고 한 것은 버린다 */
            if (b->t[i].used && b->t[i].client == client && b->t[i].req_id <= ack_upto) { b->t[i].used = false; b->size--; }
        for (int i = 0; i < DEDUP_MAX; i++)
            if (b->t[i].used && b->t[i].client == client && b->t[i].req_id == req_id) { b->hits++; return b->t[i].resp; }
    }
    b->balance += amt;                                        /* 부작용 */
    b->executions++;
    if (dedup) {
        for (int i = 0; i < DEDUP_MAX; i++)
            if (!b->t[i].used) { b->t[i] = (dedup_t){ true, client, req_id, b->balance }; b->size++; break; }
        if (b->size > b->max_size) b->max_size = b->size;
    }
    return b->balance;
}
static void bank_run(bank_t *b, bool dedup, int *retries) {
    memset(b, 0, sizeof *b);
    uint32_t acked = 0;
    *retries = 0;
    for (uint32_t id = 1; id <= 200; id++) {
        for (;;) {                                            /* 응답을 받을 때까지 같은 req_id 로 재시도 */
            if (xs32() % 10 == 0) { (*retries)++; continue; }          /* 요청 유실 */
            bank_deposit(b, dedup, 7, id, acked, 5);
            if (xs32() % 10 < 3) { (*retries)++; continue; }           /* 응답 유실: 서버는 이미 실행함 */
            break;
        }
        acked = id;
    }
}
static void ex4_dedup(void) {
    printf("--- 연습 4: at-most-once ---\n");
    bank_t b;
    int rt;
    g_rng = 99;
    bank_run(&b, false, &rt);
    printf("      중복 제거 없음: 재시도 %d, 실행 %d 번, 잔액 %lld (기대 1000)\n", rt, b.executions, (long long)b.balance);
    CHECK(b.balance > 1000);
    g_rng = 99;
    bank_run(&b, true, &rt);
    printf("      중복 제거 있음: 재시도 %d, 실행 %d 번, 캐시 응답 %d 번, 표 최대 %d 칸\n", rt, b.executions, b.hits, b.max_size);
    CHECK(b.balance == 1000 && b.executions == 200 && b.hits > 0);
    CHECK(b.max_size <= 2);                                   /* ack 로 정리되므로 표가 안 자란다 */
}

/* ===================================================================== 5. 마감 전파 */
/* 부모가 받은 마감까지 남은 시간에서 여유(margin)를 빼고, 자기 기본 마감과 작은 쪽. 0 이하면 보내지 않는다. */
static int64_t child_deadline_ms(int64_t parent_remaining_ms, int64_t own_default_ms, int64_t margin_ms) {
    int64_t budget = parent_remaining_ms - margin_ms;
    if (budget <= 0) return -1;                               /* DEADLINE_EXCEEDED: 바로 실패, 일을 만들지 않는다 */
    return budget < own_default_ms ? budget : own_default_ms;
}
static void ex5_deadline(void) {
    printf("--- 연습 5: 마감 전파 ---\n");
    CHECK(child_deadline_ms(1000, 300, 10) == 300);
    CHECK(child_deadline_ms(200, 300, 10) == 190);
    CHECK(child_deadline_ms(10, 300, 10) == -1);
    int64_t rem = 500;                                         /* 호출 사슬: 단계마다 여유 5 ms + 일 40 ms */
    int depth = 0;
    while ((rem = child_deadline_ms(rem, 1000, 5)) > 0) { rem -= 40; depth++; }
    CHECK(depth == 11);                                       /* 500 → 455 → … → 50 → 5, 다음 예산 0 → 멈춤 */
}

/* ===================================================================== 6. req_id 할당기 */
typedef struct { uint32_t next, mask; uint8_t *used; uint32_t inuse; } idalloc_t;
static bool id_alloc(idalloc_t *a, uint32_t *out) {
    if (a->inuse >= a->mask) return false;                    /* 0 제외 전부 사용 중: 루프 돌지 말고 실패 */
    do { a->next = (a->next + 1) & a->mask; } while (a->next == 0 || a->used[a->next]);
    a->used[a->next] = 1; a->inuse++;
    *out = a->next;
    return true;
}
static void id_free(idalloc_t *a, uint32_t id) { if (a->used[id]) { a->used[id] = 0; a->inuse--; } }
static void ex6_ids(void) {
    printf("--- 연습 6: req_id 랩어라운드 ---\n");
    static uint8_t used[1 << 16];
    idalloc_t a = { 0xFFF0, 0xFFFF, used, 0 };            /* 16 비트 id 로 줄여서 랩어라운드를 빨리 본다 */
    uint32_t id, held[40], nheld = 0, seq[40];
    bool ok = true;
    for (int i = 0; i < 40; i++) {
        if (!id_alloc(&a, &id) || id == 0) ok = false;
        seq[i] = id;
        if (i % 2) id_free(&a, id); else held[nheld++] = id;  /* 반은 바로 끝, 반은 계속 쓰는 중 */
    }
    CHECK(ok && seq[0] == 0xFFF1 && seq[14] == 0xFFFF && seq[15] == 1);   /* 0 을 건너뛰고 1 로 */
    a.next = 0xFFF0;                                       /* 같은 구간을 다시 돈다: 쓰는 중인 id 를 피해야 */
    bool clash = false;
    for (int i = 0; i < 10; i++) {
        if (!id_alloc(&a, &id) || id == 0) { ok = false; continue; }
        for (uint32_t k = 0; k < nheld; k++) if (held[k] == id) clash = true;
    }
    CHECK(ok && !clash && a.inuse == 30);
    while (id_alloc(&a, &id)) {}
    CHECK(a.inuse == 0xFFFF && !id_alloc(&a, &id));        /* 꽉 참 → false (무한 루프 아님) */
}

int main(void) {
    ex1_cobs();
    ex2_protobuf();
    ex3_flow();
    ex4_dedup();
    ex5_deadline();
    ex6_ids();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
