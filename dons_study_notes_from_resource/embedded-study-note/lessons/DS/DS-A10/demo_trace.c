/* DS-A10 demo: 첫 번째 컴팩션의 각 단계에서 전원을 끊고, 재마운트가 무엇을 보는지 출력 (solution.c 를 포함).
 * build: cc -std=c11 -O2 -w demo_trace.c -o dt && ./dt
 */
#define main solution_main
#include "solution.c"
#undef main

static kv_t kv;
static nor_t f;

/* 키 1,2 는 cold(값 40B), 키 3 은 hot. 첫 컴팩션 전까지 돈다 */
static int run(int64_t cut, int mode, uint32_t k) {
    uint8_t v[64];
    int i = 0, rc = KV_OK;
    nor_fresh(&f, 512, 4, 64);
    if (cut >= 0) nor_arm(&f, (uint64_t)cut, mode, k, 0);
    if (kv_mount(&kv, &f) != KV_OK) return -1;
    memset(v, 0x11, 40); if ((rc = kv_set(&kv, 1, v, 40)) != KV_OK) return i;
    memset(v, 0x22, 40); if ((rc = kv_set(&kv, 2, v, 40)) != KV_OK) return i;
    for (i = 0; i < 60; i++) {
        memset(v, 0x30 + i, 30);
        if ((rc = kv_set(&kv, 3, v, 30)) != KV_OK) return i;
    }
    return i;
}

static void report(const char *lbl) {
    uint8_t b[64]; uint16_t l = 0;
    int got1 = 0, got3 = -1;
    nor_power_on(&f);
    if (kv_mount(&kv, &f) != KV_OK) { printf("%-30s mount failed\n", lbl); return; }
    if (kv_get(&kv, 1, b, 64, &l) == KV_OK && l == 40u && b[0] == 0x11u) got1 = 1;
    if (kv_get(&kv, 3, b, 64, &l) == KV_OK) got3 = b[0];
    printf("%-30s copying=%u dead=%u garbage=%u dirty=%u | head=S%u  key1(cold)=%s key3(hot)=0x%02x\n", lbl,
           kv.st_copying, kv.st_dead, kv.st_garbage, kv.st_dirty, kv.head, got1 ? "ok" : "LOST", (unsigned)(got3 & 0xFF));
    printf("%-30s check=%s\n", "", kv_check(&kv) ? "ok" : "BAD");
}

int main(void) {
    crc_init();
    g_tr_on = true; g_tr_n = 0;
    run(-1, 0, 0);
    g_tr_on = false;
    uint32_t E = UINT32_MAX;
    for (uint32_t i = 0; i < g_tr_n; i++) if (g_tr_kind[i] == 2) { E = i; break; }
    if (E == UINT32_MAX) { puts("no compaction reached"); return 1; }
    uint32_t hdr = E;
    while (hdr > 0 && !(g_tr_kind[hdr] == 1 && g_tr_len[hdr] == 16u)) hdr--;
    printf("trace: total flash ops=%u; compaction: header op=%u, commit op=%u (len %u), erase op=%u\n",
           g_tr_n, hdr, E - 1, g_tr_len[E - 1], E);
    printf("%-30s %s\n", "cut point", "what mount saw");
    run(hdr, F_P_PREFIX, 10); report("D header torn (10/16 B)");
    run(hdr + 1, F_P_PREFIX, 8); report("copying first live rec");
    run(E - 1, F_P_PREFIX, 2); report("commit torn (2/4 B)");
    run(E - 1, F_P_PREFIX, 4); report("commit complete, then cut");
    run(E, F_E_SUFFIX_ZERO, 64); report("erase V: pre-program k=64");
    run(E, F_E_SUFFIX_FF, 64); report("erase V: suffix erased k=64");
    run(E, F_E_RANDOM, 0); report("erase V: random bits");
    run(E, F_E_DONE, 0); report("erase V: complete, then cut");
    return 0;
}
