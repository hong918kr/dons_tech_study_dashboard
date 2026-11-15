/* ALGO-M05 demo_deferred — "probe 해 보고 공급자가 없으면 미룬다(defer)" 재시도 루프 vs Kahn
 *
 * 개념 모델 (리눅스 deferred probe 를 단순화한 것 — 실제 커널 동작과 같다고 주장하지 않는다):
 *   등록 순서대로 각 드라이버의 probe 를 시도한다. 공급자가 하나라도 아직 안 올라왔으면 -EPROBE_DEFER 로 미루고
 *   미룬 목록에 넣는다. 한 바퀴에 하나라도 성공했으면 미룬 목록 전체를 다시 시도한다. 성공이 없는 바퀴가 나오면 끝.
 * Kahn 은 올라갈 수 있는 드라이버마다 probe 를 정확히 한 번 (실패 0번), 간선은 한 번씩만 본다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_deferred.c -o demo_deferred && ./demo_deferred
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXV 4096
#define MAXE 16384

static uint32_t n, m, eu[MAXE], ev[MAXE];
static uint32_t poff[MAXV + 1], padj[MAXE];          /* 선행자(공급자) 목록 CSR */
static uint32_t reg[MAXV];                           /* 등록 순서 */

static uint32_t g_rng = 0x2545F491u;
static uint32_t xs(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static void build_pred(void) {
    static uint32_t cur[MAXV];
    memset(poff, 0, sizeof(uint32_t) * (n + 1));
    for (uint32_t i = 0; i < m; i++) poff[ev[i] + 1]++;
    for (uint32_t u = 0; u < n; u++) poff[u + 1] += poff[u];
    for (uint32_t u = 0; u < n; u++) cur[u] = poff[u];
    for (uint32_t i = 0; i < m; i++) padj[cur[ev[i]]++] = eu[i];
}

/* 반환 = probe 시도 횟수, *rounds = 바퀴 수, *checks = 공급자 확인 횟수, *bound = 성공한 드라이버 수 */
static uint64_t deferred(uint64_t *rounds, uint64_t *checks, uint32_t *bound) {
    static uint8_t up[MAXV];
    static uint32_t list[MAXV], next[MAXV];
    uint32_t len = n, ok = 0;
    uint64_t tries = 0;
    memset(up, 0, n);
    memcpy(list, reg, sizeof(uint32_t) * n);
    *rounds = 0; *checks = 0;
    for (;;) {
        uint32_t nl = 0, progress = 0;
        (*rounds)++;
        for (uint32_t i = 0; i < len; i++) {
            uint32_t v = list[i], ready = 1;
            tries++;
            for (uint32_t j = poff[v]; j < poff[v + 1]; j++) { (*checks)++; if (!up[padj[j]]) { ready = 0; break; } }
            if (ready) { up[v] = 1; ok++; progress = 1; } else next[nl++] = v;
        }
        memcpy(list, next, sizeof(uint32_t) * nl); len = nl;
        if (!progress || len == 0) break;
    }
    *bound = ok;
    return tries;
}

static void report(const char *name) {
    uint64_t rounds, checks;
    uint32_t ok;
    build_pred();
    uint64_t t = deferred(&rounds, &checks, &ok);
    printf("%-34s V=%5u E=%5u | probe tries=%9llu (%.1f x V) rounds=%5llu supplier checks=%10llu | bound=%u | kahn: probes=%u (no failures)\n",
           name, n, m, (unsigned long long)t, (double)t / n, (unsigned long long)rounds, (unsigned long long)checks, ok, ok);
}

int main(void) {
    static const uint32_t sizes[] = { 10, 100, 1000, 4000 };
    for (int s = 0; s < 4; s++) {                    /* 사슬 0 -> 1 -> ... -> n-1, 의존 순서대로 등록 (최선) */
        n = sizes[s]; m = 0;
        for (uint32_t i = 0; i + 1 < n; i++) { eu[m] = i; ev[m] = i + 1; m++; }
        for (uint32_t i = 0; i < n; i++) reg[i] = i;
        char nm[64]; snprintf(nm, sizeof nm, "chain, registered in dep order");
        report(nm);
        for (uint32_t i = 0; i < n; i++) reg[i] = n - 1 - i;   /* 역순 등록 (최악) */
        snprintf(nm, sizeof nm, "chain, registered reversed");
        report(nm);
    }
    for (int s = 0; s < 4; s++) {                    /* 무작위 DAG E = 3V, 무작위 등록 순서 */
        n = sizes[s]; m = 3 * n;
        static uint32_t perm[MAXV];
        for (uint32_t i = 0; i < n; i++) perm[i] = i;
        for (uint32_t i = n; i > 1; i--) { uint32_t j = xs() % i, t = perm[i - 1]; perm[i - 1] = perm[j]; perm[j] = t; }
        for (uint32_t i = 0; i < m; i++) {
            uint32_t a = xs() % n, b = xs() % n;
            if (a == b) b = (a + 1) % n;
            if (a > b) { uint32_t t = a; a = b; b = t; }
            eu[i] = perm[a]; ev[i] = perm[b];
        }
        for (uint32_t i = 0; i < n; i++) reg[i] = i;
        for (uint32_t i = n; i > 1; i--) { uint32_t j = xs() % i, t = reg[i - 1]; reg[i - 1] = reg[j]; reg[j] = t; }
        report("random DAG E=3V, random reg order");
    }
    /* 본문 13 드라이버, 이름 알파벳순 등록 + i2c1 <-> pmic 사이클 버전 */
    enum { CLOCK, GPIO, DMA, I2C1, SPI1, PMIC, IMU, BARO, MAG, FLASH, FS, FUSION, LOGGER, NDRV };
    static const uint32_t E2[][2] = {
        {CLOCK,GPIO}, {CLOCK,DMA}, {CLOCK,I2C1}, {GPIO,I2C1}, {CLOCK,SPI1}, {GPIO,SPI1}, {DMA,SPI1}, {I2C1,PMIC},
        {SPI1,IMU}, {PMIC,IMU}, {GPIO,IMU}, {I2C1,BARO}, {PMIC,BARO}, {I2C1,MAG}, {PMIC,MAG}, {SPI1,FLASH},
        {FLASH,FS}, {IMU,FUSION}, {BARO,FUSION}, {MAG,FUSION}, {FS,LOGGER}, {FUSION,LOGGER}, {PMIC,I2C1} };
    /* 알파벳순: baro clock dma flash fs fusion gpio i2c1 imu logger mag pmic spi1 */
    static const uint32_t alpha[NDRV] = { BARO, CLOCK, DMA, FLASH, FS, FUSION, GPIO, I2C1, IMU, LOGGER, MAG, PMIC, SPI1 };
    n = NDRV; m = 22;
    for (uint32_t i = 0; i < m; i++) { eu[i] = E2[i][0]; ev[i] = E2[i][1]; }
    memcpy(reg, alpha, sizeof alpha);
    report("13 drivers, alphabetical reg");
    m = 23;
    eu[22] = E2[22][0]; ev[22] = E2[22][1];
    report("13 drivers + pmic->i2c1 cycle");
    return 0;
}
