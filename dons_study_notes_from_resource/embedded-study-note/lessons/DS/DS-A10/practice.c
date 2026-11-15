/* DS-A10 추가 연습 — 플래시 로그의 네 가지 응용 (self-contained)
 *  연습 1: 1→0 만 쓸 수 있는 워드 배열로 만든 단항(unary) 부트 카운터 — 소거 사이 쓰기 예산 최대화
 *  연습 2: CRC 가 찢어진 쓰기를 진짜로 잡는지 — 모든 prefix tear 를 전수로 확인
 *  연습 3: commit 워드 디코더 — "0xFF 아니면 커밋" 이 pre-program 잔해에 속는 것을 재현
 *  연습 4: 동기 마커 재동기화 스캔 — 중간 레코드 하나가 깨져도 뒤 레코드를 살린다 (비트 썩음 대응)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

static uint32_t crc32_buf(const void *p, size_t n) {
    const uint8_t *s = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *s++;
        for (int b = 0; b < 8; b++) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}
static uint32_t rnd(uint32_t *s) {
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* ---------- 연습 1: 단항 카운터 ----------
 * 워드 w[n] 은 소거 시 전부 1. count = (앞쪽부터 0 인 비트 수). 비트를 하나씩 0 으로 내리면 count+1.
 * n 워드면 32n 번 증가 후 소거 한 번. 이 값은 다른 키의 합 등을 쓰지 않아 "한 번의 program" 으로 끝난다.
 * 주의: 같은 워드를 여러 번 프로그램하므로 ECC 가 붙은 내장 플래시(STM32L4 등)에서는 쓸 수 없다.
 *       거기서는 워드 하나가 count 하나다 (uc_words_needed 참고). */
static uint32_t uc_read(const uint32_t *w, uint32_t n) {
    uint32_t c = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t zeros = (uint32_t)__builtin_popcount(~w[i]);
        c += zeros;
        if (zeros < 32u) break;
    }
    return c;
}
/* count 를 1 늘린다. 다 찼으면 false (호출자가 소거해야 함). 한 비트만 내린다 */
static bool uc_inc(uint32_t *w, uint32_t n) {
    uint32_t c = uc_read(w, n);
    if (c >= n * 32u) return false;
    w[c / 32u] &= ~(1u << (c % 32u));
    return true;
}
static uint32_t uc_words_needed(uint32_t increments, bool ecc_flash) {
    return ecc_flash ? increments : (increments + 31u) / 32u;
}

/* ---------- 연습 2: 찢어진 쓰기 전수 ---------- */
static bool rec_valid(const uint8_t *r, uint32_t total) {
    uint32_t c;
    memcpy(&c, r + 4, 4);
    uint8_t tmp[total];
    memcpy(tmp, r, 4); memcpy(tmp + 4, r + 8, total - 8);
    return crc32_buf(tmp, total - 4) == c;
}
static uint32_t tear_prefix_count_valid(const uint8_t *rec, uint32_t total, uint32_t *ff_suffix_tears) {
    uint8_t buf[256];
    uint32_t valid = 0;
    *ff_suffix_tears = 0;
    for (uint32_t k = 0; k < total; k++) {
        memset(buf, 0xFF, sizeof buf);
        memcpy(buf, rec, k);
        bool ok = rec_valid(buf, total);
        if (ok) {
            valid++;
            /* 진짜 안전한 경우만 있어야 한다: 안 쓴 뒤쪽이 전부 0xFF 이면 사실 완성된 레코드 */
            bool rest_ff = true;
            for (uint32_t i = k; i < total; i++) rest_ff &= (rec[i] == 0xFFu);
            if (rest_ff) (*ff_suffix_tears)++;
        }
    }
    return valid;
}
static uint32_t make_rec(uint8_t *out, uint16_t key, const uint8_t *val, uint16_t len) {
    uint32_t total = 8u + ((len + 3u) & ~3u);
    memset(out, 0xFF, total);
    memcpy(out, &key, 2); memcpy(out + 2, &len, 2);
    if (len) memcpy(out + 8, val, len);
    uint8_t tmp[total];
    memcpy(tmp, out, 4); memcpy(tmp + 4, out + 8, total - 8);
    uint32_t c = crc32_buf(tmp, total - 4);
    memcpy(out + 4, &c, 4);
    return total;
}

/* ---------- 연습 3: commit 디코더 ---------- */
static bool commit_exact(uint32_t w, uint32_t v) { return w == v; }
static bool commit_nonff(uint32_t w) { return w != 0xFFFFFFFFu; }

/* ---------- 연습 4: 재동기화 스캔 ----------
 * 레코드 앞에 동기 워드 SYNC = 0x5AFE1F2Eu 를 둔다. 4 바이트 경계로 스캔하며, SYNC 를 찾으면 레코드를 시도,
 * CRC 가 맞으면 받아들이고 그 뒤부터, 틀리면 다음 워드로 넘어간다.
 * 메인 레슨의 "첫 무효에서 멈춤" 스캐너와 비교: 깨진 레코드 하나에 뒤쪽이 전부 사라지지 않는다.
 * 대가: 레코드마다 4바이트 + payload 안의 우연한 SYNC 는 CRC 로 거른다. */
#define SYNC 0x5AFE1F2Eu
static uint32_t sync_write(uint8_t *mem, uint32_t off, uint16_t key, const uint8_t *val, uint16_t len) {
    uint32_t w = SYNC;
    memcpy(mem + off, &w, 4);
    return 4u + make_rec(mem + off + 4, key, val, len);
}
static uint32_t scan_stop_first(const uint8_t *mem, uint32_t size) {
    uint32_t off = 0, n = 0;
    while (off + 12u <= size) {
        uint32_t w; memcpy(&w, mem + off, 4);
        if (w != SYNC) break;
        uint16_t len; memcpy(&len, mem + off + 4 + 2, 2);
        uint32_t total = 8u + ((len + 3u) & ~3u);
        if (off + 4u + total > size || !rec_valid(mem + off + 4, total)) break;
        n++; off += 4u + total;
    }
    return n;
}
static uint32_t scan_resync(const uint8_t *mem, uint32_t size) {
    uint32_t off = 0, n = 0;
    while (off + 12u <= size) {
        uint32_t w; memcpy(&w, mem + off, 4);
        if (w != SYNC) { off += 4u; continue; }
        uint16_t len; memcpy(&len, mem + off + 4 + 2, 2);
        uint32_t total = 8u + ((len + 3u) & ~3u);
        if (off + 4u + total > size || len > 200u || !rec_valid(mem + off + 4, total)) { off += 4u; continue; }
        n++; off += 4u + total;
    }
    return n;
}

int main(void) {
    /* 연습 1 */
    uint32_t w[4];
    memset(w, 0xFF, sizeof w);
    CHECK(uc_read(w, 4) == 0u);
    for (uint32_t i = 0; i < 128u; i++) (void)uc_inc(w, 4);
    CHECK(uc_read(w, 4) == 128u && w[3] == 0u);
    CHECK(uc_inc(w, 4) == false);
    /* 찢어져도 카운트는 "그대로 또는 +1" (비트 하나만 내리므로) */
    memset(w, 0xFF, sizeof w);
    for (uint32_t i = 0; i < 40u; i++) (void)uc_inc(w, 4);
    uint32_t before = uc_read(w, 4), wcopy[4];
    memcpy(wcopy, w, sizeof w);
    (void)uc_inc(w, 4);   /* 완료 */
    CHECK(uc_read(wcopy, 4) == before && uc_read(w, 4) == before + 1u);
    CHECK(uc_words_needed(1000, false) == 32u && uc_words_needed(1000, true) == 1000u);
    /* 연습 2 */
    uint8_t rec[256], val[60];
    uint32_t r = 0xABCD1234u, bad_cases = 0, all_valid = 0;
    for (int t = 0; t < 2000; t++) {
        uint16_t len = (uint16_t)(1u + rnd(&r) % 60u);
        for (uint16_t i = 0; i < len; i++) val[i] = (uint8_t)rnd(&r);
        uint32_t total = make_rec(rec, (uint16_t)(rnd(&r) & 0x7FFFu), val, len), ff;
        uint32_t v = tear_prefix_count_valid(rec, total, &ff);
        all_valid += v;
        if (v != ff) bad_cases++;
    }
    CHECK(bad_cases == 0u);
    printf("  tear test: 2000 random records, %u prefix tears passed CRC (all were FF-tail=harmless)\n", all_valid);
    /* 값 끝이 0xFF 이면 tear k < total 도 valid 이고, 그때도 데이터는 완성 */
    memset(val, 0xFF, 8);
    uint32_t total = make_rec(rec, 7, val, 8), ff;
    uint32_t v = tear_prefix_count_valid(rec, total, &ff);
    CHECK(v == ff && v == 8u);
    /* 소거 상태는 레코드로 오인되지 않는다 */
    memset(rec, 0xFF, 12);
    CHECK(rec_valid(rec, 12) == false);
    /* 연습 3 */
    uint32_t cval = 0xDEADBEEFu;
    CHECK(commit_exact(cval, cval) && !commit_exact(0u, cval) && !commit_exact(0xFFFFFFFFu, cval));
    CHECK(commit_nonff(0u) == true);          /* pre-program 잔해 0x00000000 을 "커밋"으로 착각 */
    uint32_t wrong_exact = 0, wrong_nonff = 0, seed = 11;
    for (int i = 0; i < 100000; i++) {
        uint32_t mask = rnd(&seed);
        uint32_t torn = cval | mask;          /* 찢어진 프로그램: 일부 비트만 0 */
        bool done = (torn == cval);
        if (commit_exact(torn, cval) != done) wrong_exact++;
        if (commit_nonff(torn) != done) wrong_nonff++;
    }
    CHECK(wrong_exact == 0u && wrong_nonff > 90000u);
    /* 연습 4 */
    uint8_t mem[1024];
    memset(mem, 0xFF, sizeof mem);
    uint32_t off = 0;
    uint32_t first_rec_end = 0;
    for (uint16_t k = 1; k <= 12; k++) {
        memset(val, (uint8_t)k, 30);
        off += sync_write(mem, off, k, val, 30);
        if (k == 1) first_rec_end = off;
    }
    CHECK(scan_stop_first(mem, off) == 12u && scan_resync(mem, off) == 12u);
    mem[first_rec_end + 20u] ^= 0x10u;        /* 두 번째 레코드 payload 비트 썩음 */
    CHECK(scan_stop_first(mem, off) == 1u);
    CHECK(scan_resync(mem, off) == 11u);
    /* 페이로드 속 우연한 SYNC 값은 CRC 로 걸러져야 한다 */
    memset(mem, 0xFF, sizeof mem);
    { uint32_t sy = SYNC; memcpy(val, &sy, 4); }
    memset(val + 4, 0x11, 26);
    off = sync_write(mem, 0, 1, val, 30);
    off += sync_write(mem, off, 2, val, 30);
    mem[6] ^= 0x01u;                          /* 첫 레코드의 len 을 깨뜨린다 → 스캔이 payload 안 SYNC 를 만나게 */
    uint32_t got = scan_resync(mem, off);
    CHECK(got == 1u);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
