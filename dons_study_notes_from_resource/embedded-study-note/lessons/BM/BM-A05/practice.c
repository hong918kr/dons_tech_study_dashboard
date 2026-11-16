/* BM-A05 추가 연습 — 한 번에 검증
 *
 * 연습 1 — CRC-16 표 방식 + "버스트 ≤ 16 비트는 전부 잡힌다" 를 계산으로 확인 (선형성 이용)
 * 연습 2 — python(send_ymodem.py) 이 독립적으로 만든 YMODEM 스트림을 C 수신기에 재생
 * 연습 3 — 오류 없는 전송 시간을 tick 단위로 정확히 예측하는 식
 * 연습 4 — upfront erase 가 송신기 타임아웃을 부르는 경계, lazy erase 의 최대 ACK 지연
 *
 * 같은 폴더의 solution.c 를 그대로 포함한다 (YM_NO_MAIN 으로 그쪽 main 을 끈다).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr      (python3 -I send_ymodem.py 로 fixture 생성 후)
 */
#define YM_NO_MAIN 1
#include "solution.c"

static int g_pass, g_total;
static bool g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 연습 1 ---------------- */
static uint16_t crc16_tab[256];
static uint16_t crc16_table(const uint8_t *p, uint32_t n) {
    if (!crc16_tab[1])
        for (uint32_t i = 0; i < 256; i++) crc16_tab[i] = crc16_xmodem((const uint8_t[]){(uint8_t)i}, 1);
    uint16_t c = 0;
    while (n--) c = (uint16_t)((c << 8) ^ crc16_tab[(c >> 8) ^ *p++]);
    return c;
}
/* r(x) · x mod g(x), g = x^16 + x^12 + x^5 + 1 */
static uint16_t mulx(uint16_t r) { return (r & 0x8000u) ? (uint16_t)((r << 1) ^ 0x1021u) : (uint16_t)(r << 1); }

static void ex1(void) {
    uint32_t rs = 0xBEEFu, same = 0;
    for (int t = 0; t < 3000; t++) {
        uint8_t b[1100]; uint32_t n = fl_rand(&rs) % 1100u;
        for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t)fl_rand(&rs);
        same += crc16_table(b, n) == crc16_xmodem(b, n);
    }
    CHECK(same == 3000 && crc16_table((const uint8_t *)"123456789", 9) == 0x31C3u);
    /* 1029 바이트 패킷 (STX 1024 + CRC 2 를 검사하는 부분 = 1026 바이트 = 8208 비트).
     * 오류 패턴 e 가 안 잡힘 ⇔ e(x) ≡ 0 mod g. 길이 L ≤ 16 버스트 = x^s·b(x), deg b ≤ 15, b(0)=1.
     * b 의 나머지는 b 자신(≠0), x 는 mod g 에서 가역(g(0)=1) → 모든 s 에서 ≠ 0. 계산으로 확인. */
    const uint32_t bits = 1026u * 8u;
    uint64_t checked = 0, missed = 0;
    for (uint32_t b = 1; b < 0x10000u; b += 2) {             /* b(0) = 1 인 모든 길이 ≤ 16 패턴 */
        uint16_t r = (uint16_t)b;
        for (uint32_t s = 0; s + 16u <= bits; s++) { checked++; if (r == 0) missed++; r = mulx(r); }
    }
    CHECK(missed == 0 && checked == 32768ull * (bits - 15u));
    /* 길이 17 버스트: b = 1 + ... + x^16. b mod g = b ⊕ g → 안 잡히는 건 b == g 하나뿐 (2^15 중 1) */
    uint32_t miss17 = 0;
    for (uint32_t mid = 0; mid < 0x8000u; mid++) {
        uint32_t b = 0x10001u | (mid << 1);
        if (((b ^ 0x11021u) & 0x1FFFFu) == 0) miss17++;
    }
    CHECK(miss17 == 1);
    /* 2 비트 오류 x^i + x^j 가 안 잡힘 ⇔ x^(j−i) ≡ 1 mod g. x 의 위수(order)를 찾는다 */
    uint16_t r = mulx(1); uint32_t order = 1;
    while (r != 1 && order < 70000u) { r = mulx(r); order++; }
    CHECK(order == 32767u && order > bits);                   /* 패킷 안 2 비트 오류는 전부 잡힌다 */
    if (g_verbose) printf("ex1: bursts<=16 checked %llu, missed %llu; 17-bit bursts missed %u/32768; "
                          "order of x mod g = %u (> %u bits)\n", (unsigned long long)checked,
                          (unsigned long long)missed, (unsigned)miss17, (unsigned)order, (unsigned)bits);
}

/* ---------------- 연습 2 ---------------- */
static uint8_t g_fix[65536];
static void ex2(void) {
    char path[1024]; const char *f = __FILE__, *sl = strrchr(f, '/');
    if (sl) snprintf(path, sizeof path, "%.*s/ymodem_fixture.bin", (int)(sl - f), f);
    else snprintf(path, sizeof path, "ymodem_fixture.bin");
    FILE *fp = fopen(path, "rb");
    size_t n = fp ? fread(g_fix, 1, sizeof g_fix, fp) : 0;
    if (fp) fclose(fp);
    CHECK(n > 16 && memcmp(g_fix, "YMFX", 4) == 0);
    if (n <= 16) return;
    const uint8_t *p = g_fix + 4, *end = g_fix + n, *part[3]; uint32_t len[3];
    for (int i = 0; i < 3; i++) {
        if (end - p < 4) { CHECK(0); return; }
        len[i] = rd32(p); p += 4;
        if ((uint32_t)(end - p) < len[i]) { CHECK(0); return; }
        part[i] = p; p += len[i];
    }
    const uint8_t *img = part[0], *stream = part[1], *resp = part[2];
    /* python 이 만든 헤더를 C 검사기가 받아들이나 */
    img_hdr_t h;
    CHECK(img_hdr_check(img, SLOT_ADDR, SLOT_SIZE, &h) == IMG_OK && h.payload_size == len[0] - 64u &&
          crc32_update(0, img + 64, h.payload_size) == h.payload_crc32 && h.img_version == 0x01000002u);
    /* 송신 바이트열을 "선" 에 그대로 흘린다 (raw 재생, FIFO 깊이 = 스트림 전체) */
    flash_init(&g_fl, 4);
    sim_reset_raw(stream, len[1], RXQ_MAX);
    sink_init(&g_sink, &g_fl, SLOT_ADDR, SLOT_SIZE, ERASE_LAZY);
    memset(&g_ym, 0, sizeof g_ym); g_ym.sink = &g_sink;
    ym_rc_t rc = ym_receive(&g_ym);
    CHECK(rc == YM_OK && strcmp(g_ym.name, "fixture.bin") == 0 && g_ym.size == len[0]);
    CHECK(S.resp_n == len[2] && memcmp(S.resp, resp, len[2]) == 0);           /* 응답이 python 예상과 같다 */
    CHECK(slot_check() == IMG_OK && slot_equals(&g_fl, img, len[0]));
    CHECK(img[len[0] - 1] == CPMEOF && g_fl.mem[SLOT_ADDR - FL_BASE + len[0]] == 0xFF);  /* 진짜 0x1A 는 남고 패딩은 없다 */
    if (g_verbose) printf("ex2: python fixture image %u B, stream %u B, %u responses matched, rc=%s\n",
                          (unsigned)len[0], (unsigned)len[1], (unsigned)len[2], ym_rc_name(rc));
}

/* ---------------- 연습 3 ---------------- */
static uint32_t prog_ticks(uint32_t bytes) { return (bytes * g_fl_time.prog_ticks_per_kb + 1023u) / 1024u; }
/* 오류 없는 lazy-erase 전송이 끝나는 tick (= null block 0 에 ACK 한 시각) */
static uint64_t predict_ticks(uint32_t size, bool only128) {
    uint64_t t = 133;                                         /* t=0 'C' → block 0 바이트 1..133 */
    uint32_t off = 0, erased = 0;
    while (off < size) {
        uint32_t L = (!only128 && size - off >= 1024u) ? 1024u : 128u;
        uint32_t n = size - off < L ? size - off : L;          /* 잘린 길이 */
        uint32_t lo = off < 64u ? 64u : off, hi = off + n;     /* 헤더는 RAM 으로 */
        t += L + 5u;                                           /* 패킷 바이트 = tick */
        if (hi > lo) {
            for (uint32_t s = lo / FL_SECTOR; s <= (hi - 1u) / FL_SECTOR; s++)
                if (!(erased >> s & 1u)) { erased |= 1u << s; t += g_fl_time.erase_ticks; }
            t += prog_ticks((hi - lo + 7u) / 8u * 8u);
        }
        off += L;
    }
    t += 1u + 1u + prog_ticks(64) + 133u;                     /* EOT, EOT(+헤더 commit), null block 0 */
    return t;
}
static void ex3(void) {
    const uint32_t sizes[] = {65, 200, 1024, 1088, 16384, 16448, 33000, 65536, SLOT_SIZE};
    uint32_t match = 0, cases = 0;
    static uint8_t img[SLOT_SIZE];
    for (int m = 0; m < 2; m++)
        for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
            fault_t fx = fault_none(); fx.only_128 = m;
            uint32_t n = make_image(img, sizes[k] - 64u, 1, k + 1u, 0);
            flash_init(&g_fl, 1);
            ym_rc_t rc = xfer(img, n, &fx, ERASE_LAZY);
            uint64_t want = predict_ticks(n, m);
            match += rc == YM_OK && sim_now() == want; cases++;
            if (g_verbose && (sizes[k] == 65536u || sizes[k] == SLOT_SIZE))
                printf("ex3: %6u B %-4s: %llu ticks = %llu ms, %.1f%% of 11520 B/s line rate\n", (unsigned)n,
                       m ? "128" : "1K", (unsigned long long)want, (unsigned long long)(want * 1000u / TICKS_PER_S),
                       100.0 * n / (double)want);
        }
    CHECK(match == cases);
}

/* ---------------- 연습 4 ---------------- */
static void ex4(void) {
    static uint8_t img[SLOT_SIZE];
    uint32_t n = make_image(img, 40000, 1, 9, 0);
    fault_t fx = fault_none();
    fl_timing_t keep = g_fl_time;
    /* upfront-slot: block 0 의 ACK 는 6·t_erase 늦어진다. 송신기 타임아웃 SND_TO ≤ 6·t_erase 이면 재전송 */
    const uint32_t edge = SND_TO / 6u;                        /* 19200 tick = 1.667 s */
    g_fl_time.erase_ticks = edge - 1u; flash_init(&g_fl, 1);
    CHECK(xfer(img, n, &fx, ERASE_UPFRONT_SLOT) == YM_OK && S.s_timeouts == 0 && S.max_stall == 6u * (edge - 1u));
    g_fl_time.erase_ticks = edge; flash_init(&g_fl, 1);
    ym_rc_t rc = xfer(img, n, &fx, ERASE_UPFRONT_SLOT);
    CHECK(rc == YM_OK && S.s_timeouts >= 1 && g_ym.st.dups >= 1 && slot_equals(&g_fl, img, n));
    if (g_verbose) printf("ex4: erase %u ticks x6 >= SND_TO: sender timeouts %u, dup %u, overruns %u, rc=%s\n",
                          (unsigned)edge, (unsigned)S.s_timeouts, (unsigned)g_ym.st.dups, (unsigned)S.overruns,
                          ym_rc_name(rc));
    /* lazy: 최대 ACK 지연 = 한 섹터 erase + 1 KB program */
    g_fl_time = keep; flash_init(&g_fl, 1);
    CHECK(xfer(img, n, &fx, ERASE_LAZY) == YM_OK && S.max_stall == g_fl_time.erase_ticks + prog_ticks(1024));
    g_fl_time = keep;
    (void)slot_hdr_blank; (void)boot_region_intact; (void)n_data_blocks;
}

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
