/* COMM-A06 demo_nor.c — 시뮬레이션 시간(1 tick = 1 µs, 칩 시간은 W25Q 류 typical 대표값)으로 재는 세 실험.
 *   1) erase 전략: 4 KB 섹터만 vs 64 KB 블록 섞기 — 명령 수와 걸린 시간
 *   2) 64 KB 쓰기: RDSR 폴링 간격에 따른 총 시간, 폴링 횟수, 버스 점유, 정렬 안 된 시작 주소의 PP 개수
 *   3) 로그 저장소 전원 차단 캠페인 2000 회: 차단 순간 칩이 하던 일 × 재마운트 결과
 * solution.c 를 그대로 포함한다 (main 이름만 바꿈).
 * 빌드: cc -std=c11 -O2 demo_nor.c -o demo_nor && ./demo_nor
 */
#define main solution_main
#include "solution.c"
#undef main

static void erase_row(const char *name, uint32_t addr, uint32_t len) {
    uint32_t t0, se0, be0, ms_sec, ms_mix, cmd_sec, cmd_mix;
    fresh();
    se0 = g_chip.n_exec[OP_SE]; t0 = g_now;
    for (uint32_t a = addr; a < addr + len; a += NOR_SECTOR) nor_erase(&g_dev, a, NOR_SECTOR);   /* 섹터만 */
    cmd_sec = g_chip.n_exec[OP_SE] - se0; ms_sec = (g_now - t0) / 1000u;
    fresh();
    se0 = g_chip.n_exec[OP_SE]; be0 = g_chip.n_exec[OP_BE]; t0 = g_now;
    nor_erase(&g_dev, addr, len);                                                               /* 드라이버: 블록 섞기 */
    cmd_mix = (g_chip.n_exec[OP_SE] - se0) + (g_chip.n_exec[OP_BE] - be0); ms_mix = (g_now - t0) / 1000u;
    printf("| %-22s | %4u SE | %7u ms | %2u BE + %3u SE | %6u ms | %4.1fx |\n", name, (unsigned)cmd_sec, (unsigned)ms_sec,
           (unsigned)(g_chip.n_exec[OP_BE] - be0), (unsigned)(cmd_mix - (g_chip.n_exec[OP_BE] - be0)), (unsigned)ms_mix,
           ms_mix ? (double)ms_sec / ms_mix : 0.0);
}

static void write_row(uint32_t poll, uint32_t addr) {
    static uint8_t data[65536];
    for (uint32_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 13);
    fresh();
    uint32_t t0 = g_now, pp0 = g_chip.n_exec[OP_PP], polls0 = g_dev.polls;
    uint32_t a = addr, left = sizeof data;
    const uint8_t *b = data;
    while (left) {                                                 /* nor_write 와 같은 분할, 폴링 간격만 바꿈 */
        uint32_t n = NOR_PAGE - (a & (NOR_PAGE - 1));
        if (n > left) n = left;
        nor_write_enable(&g_dev);
        spi_select(&g_dev); spi_byte(&g_dev, CMD_PP); nor_send_addr(&g_dev, a);
        for (uint32_t i = 0; i < n; i++) spi_byte(&g_dev, b[i]);
        spi_deselect(&g_dev);
        nor_wait_ready(&g_dev, TO_PP, poll);
        a += n; b += n; left -= n;
    }
    uint32_t polls = g_dev.polls - polls0, ticks = g_now - t0, pps = g_chip.n_exec[OP_PP] - pp0;
    uint32_t bus = pps * (1 + 4) + polls * 2 + (uint32_t)sizeof data;   /* WREN 1 B, PP 헤더 4 B, RDSR 2 B, 데이터 */
    int ok = memcmp(g_chip.mem + addr, data, sizeof data) == 0;
    printf("| %6u | 0x%05X | %4u | %6u | %6.1f ms | %6.1f KB/s | %5.1f %% | %s |\n", (unsigned)poll, (unsigned)addr,
           (unsigned)pps, (unsigned)polls, ticks / 1000.0, 65536.0 / 1024.0 / (ticks / 1e6), 100.0 * bus / ticks,
           ok ? "OK" : "BAD");
}

int main(void) {
    printf("## 1) erase strategy (sim time, typ: t_SE %u ms, t_BE %u ms)\n\n", T_SE / 1000, T_BE / 1000);
    printf("| range                  | sector-only        | driver (mixed)            | speedup |\n|---|---|---|---|\n");
    erase_row("4 KB @0x00000", 0x00000, 0x1000);
    erase_row("64 KB @0x10000", 0x10000, 0x10000);
    erase_row("64 KB @0x11000 (unal.)", 0x11000, 0x10000);
    erase_row("256 KB @0x40000", 0x40000, 0x40000);
    erase_row("1 MB @0", 0, NOR_MAX_SIZE);

    printf("\n## 2) 64 KB write vs RDSR poll interval (t_PP %u us, SPI 1 B = 1 us)\n\n", T_PP);
    printf("| poll us | start | PPs | polls | time | throughput | bus busy | data |\n|---|---|---|---|---|---|---|---|\n");
    const uint32_t polls[] = { 5, 50, 200, 700, 1000 };
    for (unsigned i = 0; i < 5; i++) write_row(polls[i], 0x20000);
    write_row(50, 0x20010);

    printf("\n## 3) log store power-cut campaign (4 sectors @0x80000)\n\n");
    LogStore ls;
    fresh();
    log_mount(&ls, &g_dev, 0x80000, 4);
    static uint8_t  acked[C_MAX_ATTEMPTS];
    static uint32_t acked_seq[C_MAX_ATTEMPTS];
    uint32_t s = 0x5EED5u, att = 0, by_op[OP_COUNT] = { 0 }, torn[OP_COUNT] = { 0 }, rot[OP_COUNT] = { 0 };
    uint32_t lost = 0, bad = 0, cuts = 0, alive_sum = 0, unacked_present = 0;
    uint8_t p[LOG_MAX_PAYLOAD];
    for (int cyc = 0; cyc < 2000 && att + 500 < C_MAX_ATTEMPTS; cyc++) {
        uint32_t win = xs32(&s) % 2 ? 3000u : 200000u;
        g_chip.cut_armed = true; g_chip.cut_at = g_now + 1 + xs32(&s) % win;
        uint32_t last_try = 0;
        for (int k = 0; k < 500 && g_chip.powered; k++) {
            uint32_t len = make_payload(att, p);
            last_try = att;
            int rc = log_append(&ls, p, len);
            if (rc == NOR_OK) { acked[att] = 1; acked_seq[att] = ls.seq; }
            att++;
            if (rc != NOR_OK) break;
        }
        if (g_chip.powered) continue;
        cuts++;
        uint8_t op = g_chip.cut_op;
        by_op[op]++;
        chip_power_on(&g_chip);
        log_mount(&ls, &g_dev, 0x80000, 4);
        torn[op] += ls.n_corrupt > 0; rot[op] += ls.need_rotate || ls.wp == 0x80000 + (ls.cur + 1) * NOR_SECTOR;
        memset(&g_col, 0, sizeof g_col);
        log_iterate(&ls, collect_cb, &g_col);
        bad += g_col.bad + g_col.order_bad;
        alive_sum += g_col.n;
        for (uint32_t k = 0; k < g_col.n && k < 2048; k++) if (g_col.idx[k] == last_try && !acked[last_try]) unacked_present++;
        uint32_t k = 0;
        for (uint32_t i = 0; i < att; i++) {
            if (!acked[i] || acked_seq[i] + 2 < ls.seq) continue;
            while (k < g_col.n && g_col.idx[k] < i) k++;
            if (k >= g_col.n || g_col.idx[k] != i) lost++;
        }
    }
    const char *nm[OP_COUNT] = { "idle / SPI frame", "page program", "sector erase", "block erase", "WRSR" };
    printf("| cut during | cuts | torn record seen (CRC) | next append rotates |\n|---|---|---|---|\n");
    for (int o = 0; o < OP_COUNT; o++)
        if (by_op[o]) printf("| %s | %u | %u | %u |\n", nm[o], (unsigned)by_op[o], (unsigned)torn[o], (unsigned)rot[o]);
    printf("\ncuts %u, appends tried %u, lost acked (guaranteed window) %u, corrupt/out-of-order returned %u,\n"
           "unacked (in-flight) record found complete after reboot %u, avg alive records %.1f\n",
           (unsigned)cuts, (unsigned)att, (unsigned)lost, (unsigned)bad, (unsigned)unacked_present, (double)alive_sum / cuts);
    return 0;
}
