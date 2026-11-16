/* BM-A09 추가 연습 — 호스트 C (맥 cc 또는 VM 의 gcc). 펌웨어 없이 사후 분석 "도구 쪽" 판단을 검증한다.
 *   연습 1  덤프 링: 최근 4 개 보관 (seq 번호, 가장 새 유효본이 이긴다, 찢어진 쓰기에도 이전 것은 산다)
 *   연습 2  버전 호환 리더: v1 (32 B) 과 v2 (204 B) 를 모두 읽어 공통 뷰로, 모르는 버전은 거절
 *   연습 3  크래시 시그니처: 같은 버그 = 같은 서명 (레지스터 잡음 무시), 다른 PC = 다른 서명
 *   연습 4  전송 무결성: @@HEX 줄로 인코딩 → 디코딩, 글자 손상/줄 누락/줄 중복/줄 뒤바뀜을 전부 거절
 *   연습 5  오수용 확률: 함대 규모에서 "쓰레기를 덤프로 믿는" 기대 횟수
 * cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A09_pr && /tmp/BM-A09_pr
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int pass, total;
#define CHECK(expr) do { total++; if (expr) pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 0xA09A09u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

/* ---------- (BM-A09 덤프 형식 축약판: 같은 204 B 레이아웃, 같은 CRC-32) ---------- */
#define DUMP_MAGIC 0x504D5544u
#define DUMP_NSTACK 16u
typedef struct {
    uint32_t magic, version, size, boot_count, reset_reason, exc, exc_return, flags, stage, sp, msp, psp;
    uint32_t r[13];
    uint32_t lr, pc, xpsr, cfsr, hfsr, mmfar, bfar, shcsr, nstack;
    uint32_t stack[DUMP_NSTACK];
    uint32_t crc;
} crash_dump_t;
_Static_assert(sizeof(crash_dump_t) == 204, "v2");
typedef struct { uint32_t magic, version, size, exc, pc, lr, cfsr, crc; } crash_dump_v1_t;
_Static_assert(sizeof(crash_dump_v1_t) == 32, "v1");

static uint32_t crc32(const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static void seal(crash_dump_t *d) { d->magic = 0u; d->crc = crc32((uint8_t *)d + 4, 196u); d->magic = DUMP_MAGIC; }
static int valid(const crash_dump_t *d) {
    return d->magic == DUMP_MAGIC && d->version == 2u && d->size == 204u && d->crc == crc32((const uint8_t *)d + 4, 196u);
}
static void make_dump(crash_dump_t *d, uint32_t exc, uint32_t pc, uint32_t lr, uint32_t cfsr) {
    memset(d, 0, sizeof *d);
    d->version = 2u; d->size = 204u; d->exc = exc; d->pc = pc; d->lr = lr; d->cfsr = cfsr; d->stage = 1u;
    seal(d);
}

/* ================= 연습 1: 덤프 링 =================
 * 슬롯 4 개. 각 슬롯 = { seq, dump }. 새 덤프는 "가장 오래된(또는 무효인) 슬롯" 에 seq = 최대 seq + 1 로.
 * 쓰는 순서: 슬롯 magic 무효화 → seq·내용 → 봉인 (seq 도 CRC 범위 안: dump.boot_count 자리를 seq 로 쓴다).
 * ring_newest(): 유효한 것 중 seq 최대. ring_count(): 유효 개수. */
#define RING_N 4u
typedef struct { crash_dump_t slot[RING_N]; } ring_t;
static uint32_t ring_pick_slot(const ring_t *r) {
    uint32_t best = 0, best_seq = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < RING_N; ++i) {
        if (!valid(&r->slot[i])) return i;                       /* 빈/깨진 슬롯 먼저 */
        if (r->slot[i].boot_count < best_seq) { best_seq = r->slot[i].boot_count; best = i; }
    }
    return best;
}
static int ring_newest(const ring_t *r) {
    int best = -1;
    for (uint32_t i = 0; i < RING_N; ++i)
        if (valid(&r->slot[i]) && (best < 0 || r->slot[i].boot_count > r->slot[best].boot_count)) best = (int)i;
    return best;
}
static uint32_t ring_count(const ring_t *r) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < RING_N; ++i) n += (uint32_t)valid(&r->slot[i]);
    return n;
}
/* torn_after_fields: 1 이면 내용을 쓴 뒤 봉인 전에 "전원이 꺼진" 것처럼 멈춘다 */
static void ring_push(ring_t *r, const crash_dump_t *src, int torn_after_fields) {
    int nw = ring_newest(r);
    uint32_t seq = nw < 0 ? 1u : r->slot[nw].boot_count + 1u;
    crash_dump_t *d = &r->slot[ring_pick_slot(r)];
    d->magic = 0u;                                              /* 1) 이 슬롯을 먼저 무효로 */
    uint32_t keep_magic = d->magic;
    memcpy(d, src, sizeof *d);
    d->magic = keep_magic;                                      /* 2) 내용 (magic 은 아직 0) */
    d->boot_count = seq;
    if (torn_after_fields) return;
    seal(d);                                                    /* 3) 봉인 */
}

/* ================= 연습 2: 버전 호환 리더 ================= */
typedef struct { uint32_t version, exc, pc, lr, cfsr; } dump_view_t;
enum { RD_OK = 0, RD_SHORT, RD_NO_MAGIC, RD_UNKNOWN_VERSION, RD_BAD_SIZE, RD_BAD_CRC };
static int dump_read_any(const uint8_t *buf, size_t len, dump_view_t *out) {
    uint32_t h[3];
    if (len < 12u) return RD_SHORT;
    memcpy(h, buf, 12u);
    if (h[0] != DUMP_MAGIC) return RD_NO_MAGIC;
    if (h[1] == 1u) {
        crash_dump_v1_t v;
        if (h[2] != sizeof v) return RD_BAD_SIZE;
        if (len < sizeof v) return RD_SHORT;
        memcpy(&v, buf, sizeof v);
        if (v.crc != crc32(buf + 4, 24u)) return RD_BAD_CRC;
        *out = (dump_view_t){ 1u, v.exc, v.pc, v.lr, v.cfsr };
        return RD_OK;
    }
    if (h[1] == 2u) {
        crash_dump_t d;
        if (h[2] != sizeof d) return RD_BAD_SIZE;
        if (len < sizeof d) return RD_SHORT;
        memcpy(&d, buf, sizeof d);
        if (d.crc != crc32(buf + 4, 196u)) return RD_BAD_CRC;
        *out = (dump_view_t){ 2u, d.exc, d.pc, d.lr, d.cfsr };
        return RD_OK;
    }
    return RD_UNKNOWN_VERSION;
}

/* ================= 연습 3: 크래시 시그니처 =================
 * 서명 = CRC-32( exc, CFSR 의 "원인" 비트 (VALID 비트 제외), pc, lr (EXC_RETURN 이면 그대로, 코드 주소면 bit0 제거) ).
 * r0–r12, SP, 스택 워드, 부팅 번호, BFAR/MMFAR 값은 넣지 않는다 (같은 버그라도 매번 다르다). */
static uint32_t crash_sig(const crash_dump_t *d) {
    uint32_t k[4];
    k[0] = d->exc;
    k[1] = d->cfsr & ~((1u << 7) | (1u << 15));        /* MMARVALID, BFARVALID 제외 */
    k[2] = d->pc;
    k[3] = (d->lr >> 8 == 0x00FFFFFFu) ? d->lr : (d->lr & ~1u);
    return crc32(k, sizeof k);
}

/* ================= 연습 4: @@HEX 줄 인코딩/디코딩 ================= */
#define LINE_BYTES 32u
#define MAX_LINES 8u
typedef struct { char line[MAX_LINES][2 * LINE_BYTES + 1]; uint32_t n; } hexblock_t;
static void hex_encode(const crash_dump_t *d, hexblock_t *hb) {
    const uint8_t *p = (const uint8_t *)d;
    hb->n = 0;
    for (uint32_t off = 0; off < sizeof *d; off += LINE_BYTES, hb->n++) {
        char *o = hb->line[hb->n];
        uint32_t i = off;
        for (; i < off + LINE_BYTES && i < sizeof *d; ++i) { *o++ = "0123456789abcdef"[p[i] >> 4]; *o++ = "0123456789abcdef"[p[i] & 15u]; }
        *o = 0;
    }
}
static int hexval(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
/* 줄을 이어 붙여 바이트로 → 길이 == 204, magic/version/size/CRC 확인. 성공 1. */
static int hex_decode(const hexblock_t *hb, crash_dump_t *out) {
    uint8_t buf[MAX_LINES * LINE_BYTES];
    size_t n = 0;
    for (uint32_t l = 0; l < hb->n; ++l)
        for (const char *s = hb->line[l]; s[0]; s += 2) {
            int hi = hexval(s[0]), lo = s[1] ? hexval(s[1]) : -1;
            if (hi < 0 || lo < 0 || n >= sizeof buf) return 0;
            buf[n++] = (uint8_t)(hi << 4 | lo);
        }
    if (n != sizeof *out) return 0;
    memcpy(out, buf, sizeof *out);
    return valid(out);
}

/* ================= 연습 5: 함대 오수용 기대값 =================
 * 콜드 부팅 RAM 이 균일 무작위라고 가정: P = 2^-32 (magic) · 2^-32 (version) · 2^-32 (size) · 2^-check_bits.
 * 반환 = devices · boots_per_day · 365 · P (1 년 기대 오수용 횟수). */
static double fleet_false_accepts_per_year(double devices, double boots_per_day, int header_words, int check_bits) {
    double p = 1.0;
    for (int i = 0; i < header_words; ++i) p /= 4294967296.0;
    for (int i = 0; i < check_bits; ++i) p /= 2.0;
    return devices * boots_per_day * 365.0 * p;
}

int main(void) {
    crash_dump_t d, e;
    /* --- 연습 1 --- */
    static ring_t ring;
    memset(&ring, 0, sizeof ring);
    CHECK(ring_newest(&ring) < 0 && ring_count(&ring) == 0u);
    for (uint32_t i = 1; i <= 10u; ++i) { make_dump(&d, 6u, 0x200u + i, 0x1001u, 1u << 16); ring_push(&ring, &d, 0); }
    CHECK(ring_count(&ring) == 4u);
    CHECK(ring.slot[ring_newest(&ring)].pc == 0x20Au && ring.slot[ring_newest(&ring)].boot_count == 10u);
    uint32_t pcs = 0;
    for (uint32_t i = 0; i < RING_N; ++i) pcs |= 1u << (ring.slot[i].pc - 0x200u);
    CHECK(pcs == ((1u << 7) | (1u << 8) | (1u << 9) | (1u << 10)));       /* 최근 4 개 = 7..10 */
    make_dump(&d, 5u, 0x999u, 0x1001u, 0x8200u);
    ring_push(&ring, &d, 1);                                               /* 찢어진 쓰기 */
    CHECK(ring_count(&ring) == 3u && ring.slot[ring_newest(&ring)].pc == 0x20Au);   /* 가장 오래된 7 만 잃음 */
    ring_push(&ring, &d, 0);                                               /* 다시 시도: 깨진 슬롯을 재사용 */
    CHECK(ring_count(&ring) == 4u && ring.slot[ring_newest(&ring)].pc == 0x999u);
    CHECK(ring.slot[ring_newest(&ring)].boot_count == 11u);
    int r = ring_newest(&ring);
    ring.slot[r].pc ^= 4u;                                                 /* 최신본 손상 → 그 전 것이 최신 */
    CHECK(ring.slot[ring_newest(&ring)].pc == 0x20Au);
    /* --- 연습 2 --- */
    dump_view_t v;
    crash_dump_v1_t v1 = { 0u, 1u, 32u, 6u, 0x1234u, 0x1001u, 1u << 16, 0u };
    v1.crc = crc32(&v1.version, 24u); v1.magic = DUMP_MAGIC;
    CHECK(dump_read_any((const uint8_t *)&v1, sizeof v1, &v) == RD_OK && v.version == 1u && v.pc == 0x1234u);
    make_dump(&d, 4u, 0x274u, 0x1bfbu, 0x92u);
    CHECK(dump_read_any((const uint8_t *)&d, sizeof d, &v) == RD_OK && v.version == 2u && v.cfsr == 0x92u);
    CHECK(dump_read_any((const uint8_t *)&d, 100u, &v) == RD_SHORT);
    CHECK(dump_read_any((const uint8_t *)&v1, 8u, &v) == RD_SHORT);
    v1.version = 3u;  CHECK(dump_read_any((const uint8_t *)&v1, sizeof v1, &v) == RD_UNKNOWN_VERSION);
    v1.version = 1u; v1.size = 204u; CHECK(dump_read_any((const uint8_t *)&v1, sizeof v1, &v) == RD_BAD_SIZE);
    v1.size = 32u; v1.pc ^= 1u; CHECK(dump_read_any((const uint8_t *)&v1, sizeof v1, &v) == RD_BAD_CRC);
    e = d; e.version = 1u;                                                 /* v2 몸통에 v1 표시 → 크기 불일치 */
    CHECK(dump_read_any((const uint8_t *)&e, sizeof e, &v) == RD_BAD_SIZE);
    /* --- 연습 3: 버그 3 개 × 잡음 1000 건 → 서명 정확히 3 개 --- */
    static const uint32_t bug[3][4] = {           /* exc, cfsr, pc, lr — 이 레슨 QEMU 실행의 실제 값 */
        { 6u, 0x00010000u, 0x00000274u, 0x00001d47u },   /* UDF_MSP */
        { 5u, 0x00008200u, 0x000002e0u, 0x00001c47u },   /* BUS_PRECISE */
        { 6u, 0x00010000u, 0x00000314u, 0xFFFFFFF9u },   /* IRQ_UDF (lr = EXC_RETURN) */
    };
    uint32_t sigs[3] = { 0 }, distinct_ok = 1u, mismatch = 0u;
    for (uint32_t i = 0; i < 3000u; ++i) {
        uint32_t b = i % 3u;
        make_dump(&d, bug[b][0], bug[b][2], bug[b][3], bug[b][1]);
        for (uint32_t j = 0; j < 13u; ++j) d.r[j] = xs32();
        d.sp = 0x20000000u + (xs32() & 0xFFF0u); d.boot_count = xs32(); d.bfar = xs32();
        if (b == 1u && (xs32() & 1u)) d.cfsr &= ~(1u << 15);             /* BFARVALID 가 있을 때도, 없을 때도 */
        if (b != 2u && (xs32() & 1u)) d.lr &= ~1u;                       /* bit0 차이는 같은 복귀 주소 */
        uint32_t s = crash_sig(&d);
        if (i < 3u) sigs[b] = s; else if (s != sigs[b]) mismatch++;
    }
    distinct_ok = sigs[0] != sigs[1] && sigs[1] != sigs[2] && sigs[0] != sigs[2];
    printf("  ex3: 3000 noisy crashes from 3 bugs -> signatures %08x %08x %08x, mismatches %u\n",
           sigs[0], sigs[1], sigs[2], mismatch);
    CHECK(mismatch == 0u && distinct_ok);
    make_dump(&d, 6u, 0x274u, 0x1bfbu, 0x10000u); make_dump(&e, 6u, 0x276u, 0x1bfbu, 0x10000u);
    CHECK(crash_sig(&d) != crash_sig(&e));                                 /* PC 2 바이트 차이 = 다른 버그 */
    /* --- 연습 4: 손상 전송 20000 건 → 미검출 0 --- */
    hexblock_t hb, hc;
    make_dump(&d, 6u, 0x274u, 0x1bfbu, 0x10000u);
    for (uint32_t i = 0; i < 16u; ++i) d.stack[i] = xs32();
    seal(&d);
    hex_encode(&d, &hb);
    CHECK(hb.n == 7u && strlen(hb.line[6]) == 24u);                       /* 6 × 32 B + 12 B */
    CHECK(hex_decode(&hb, &e) && memcmp(&d, &e, sizeof d) == 0);
    uint32_t undetected = 0, kinds[4] = { 0 };
    for (uint32_t t = 0; t < 20000u; ++t) {
        hc = hb;
        uint32_t kind = t % 4u, l = xs32() % hb.n;
        kinds[kind]++;
        if (kind == 0) {                                                   /* 글자 하나를 다른 hex 글자로 */
            size_t len = strlen(hc.line[l]), pos = xs32() % len;
            char old = hc.line[l][pos], nc;
            do { nc = "0123456789abcdef"[xs32() & 15u]; } while (nc == old);
            hc.line[l][pos] = nc;
        } else if (kind == 1) {                                            /* 줄 하나 누락 */
            for (uint32_t k = l; k + 1u < hc.n; ++k) memcpy(hc.line[k], hc.line[k + 1u], sizeof hc.line[k]);
            hc.n--;
        } else if (kind == 2) {                                            /* 줄 하나 중복 */
            if (hc.n >= MAX_LINES) continue;
            for (uint32_t k = hc.n; k > l; --k) memcpy(hc.line[k], hc.line[k - 1u], sizeof hc.line[k]);
            hc.n++;
        } else {                                                           /* 32 B 줄 두 개 뒤바뀜 */
            uint32_t a = xs32() % 6u, b2 = (a + 1u + xs32() % 5u) % 6u;
            char tmp[2 * LINE_BYTES + 1];
            memcpy(tmp, hc.line[a], sizeof tmp); memcpy(hc.line[a], hc.line[b2], sizeof tmp); memcpy(hc.line[b2], tmp, sizeof tmp);
        }
        if (hex_decode(&hc, &e)) undetected++;
    }
    printf("  ex4: 20000 corrupted transfers (char %u, drop %u, dup %u, swap %u) -> undetected %u\n",
           kinds[0], kinds[1], kinds[2], kinds[3], undetected);
    CHECK(undetected == 0u);
    /* --- 연습 5 --- */
    double y32 = fleet_false_accepts_per_year(1e6, 10.0, 1, 32);   /* magic + CRC 만 */
    double y0 = fleet_false_accepts_per_year(1e6, 10.0, 1, 0);     /* magic 만 */
    double yfull = fleet_false_accepts_per_year(1e6, 10.0, 3, 32); /* magic + version + size + CRC */
    printf("  ex5: 1e6 devices x 10 boots/day: magic only %.3g/yr, magic+CRC %.3g/yr, magic+ver+size+CRC %.3g/yr\n",
           y0, y32, yfull);
    CHECK(y0 > 0.5 && y0 < 1.0);                                    /* 3.65e9 / 4.29e9 ≈ 0.85 회/년 */
    CHECK(y32 < 1e-9 && yfull < y32);
    printf("%d/%d passed\n", pass, total);
    return pass == total ? 0 : 1;
}
