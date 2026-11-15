/* ARCH-M13 demo — 정답 모델(solution.c)로 파형 · 지연 표 · VCD 를 찍는다. 체크 없음, 관찰용.
 *
 *   ./demo wave            텍스트 파형 3개: SRAM 1 WS / APB 브리지(PREADY 1 wait) / PCLK=HCLK/2 + 응답 레지스터
 *   ./demo table           접근 지연 표: 대상 × 설정 → 1회 지연, N=8·32 연속 사이클, 정상 상태 주기
 *                          (pass = PREADY/PRDATA 통과형 브리지, regR = 응답 레지스터형, /k = PCLK 분주)
 *   ./demo phase           PCLK/4 에서 같은 APB 쓰기의 지연이 "언제 냈나" 에 따라 달라지는 것
 *   ./demo vcd [경로]      VCD 파일 (기본 /tmp/esn_ARCH-M13/m13.vcd) — Surfer / GTKWave 로 연다
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bus.c -o demo && ./demo wave
 */
#define main solution_main          /* solution.c 의 테스트 main 은 이름만 바꿔서 같이 링크 */
#include "solution.c"
#undef main

static cyc_t d_w[512];
static char d_txt[16384];

static void show(const char *title, const bus_cfg_t *cf, const ahb_req_t *rq, int n, int apb) {
    long c = sim_run(cf, rq, n, d_w, 512, NULL);
    printf("%s  → %ld 사이클 (TLM 예측 %ld)\n", title, c, tlm_cycles(cf, rq, n));
    wave_format(d_w, c, apb, d_txt, sizeof d_txt);
    printf("%s\n", d_txt);
}

static void demo_wave(void) {
    ahb_req_t rq[4];
    mem_reset();
    bus_cfg_t a = { 1, 0, 1, 0 };
    rq[0] = W(SRAM_BASE + 0x10, 0x1234); rq[1] = R(SRAM_BASE + 0x10); rq[2] = W(SRAM_BASE + 0x14, 0xBEEF);
    show("[1] AHB SRAM 1 WS: W, R(같은 주소), W", &a, rq, 3, 0);

    mem_reset();
    g_apb[1] = 0xC0DE;
    bus_cfg_t b = { 0, 1, 1, 0 };
    rq[0] = W(APB_BASE + 0x0, 0x11); rq[1] = W(APB_BASE + 0x8, 0x22); rq[2] = R(APB_BASE + 0x4);
    show("[2] AHB→APB 브리지 (통과형), PREADY 1 wait: W, W, R", &b, rq, 3, 1);

    mem_reset();
    g_apb[4] = 1;
    bus_cfg_t c = { 0, 0, 2, 1 };
    rq[0] = W(APB_BASE + 0x10, 0); rq[1] = R(APB_BASE + 0x10);
    show("[3] PCLK = HCLK/2, 응답 레지스터형 브리지: 플래그 클리어 W 후 되읽기 R", &c, rq, 2, 1);
}

static void demo_table(void) {
    static const struct { const char *name; uint32_t base; bus_cfg_t cf; } rows[] = {
        { "AHB SRAM 0 WS",            SRAM_BASE, { 0, 0, 1, 0 } },
        { "AHB SRAM 1 WS",            SRAM_BASE, { 1, 0, 1, 0 } },
        { "APB /1 pass  PREADY-wait 0",   APB_BASE,  { 0, 0, 1, 0 } },
        { "APB /1 pass  PREADY-wait 1",   APB_BASE,  { 0, 1, 1, 0 } },
        { "APB /1 regR  PREADY-wait 0",   APB_BASE,  { 0, 0, 1, 1 } },
        { "APB /2 pass  PREADY-wait 0",   APB_BASE,  { 0, 0, 2, 0 } },
        { "APB /2 regR  PREADY-wait 0",   APB_BASE,  { 0, 0, 2, 1 } },
        { "APB /4 pass  PREADY-wait 0",   APB_BASE,  { 0, 0, 4, 0 } },
        { "APB /4 regR  PREADY-wait 1",   APB_BASE,  { 0, 1, 4, 1 } },
    };
    static ahb_req_t rq[32];
    printf("%-28s %6s %6s %6s %10s\n", "target / config", "single", "N=8", "N=32", "cyc/access");
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        for (int k = 0; k < 32; k++) rq[k] = W(rows[i].base + 4u * (uint32_t)k, (uint32_t)k);
        long c1 = sim_run(&rows[i].cf, rq, 1, NULL, 0, NULL);
        long c8 = sim_run(&rows[i].cf, rq, 8, NULL, 0, NULL);
        long c32 = sim_run(&rows[i].cf, rq, 32, NULL, 0, NULL);
        long per = rows[i].base == SRAM_BASE ? 1 + (long)rows[i].cf.sram_ws : apb_period(&rows[i].cf);
        printf("%-28s %6ld %6ld %6ld %10ld\n", rows[i].name, c1, c8, c32, per);
    }
}

static void demo_phase(void) {
    bus_cfg_t cf = { 0, 0, 4, 0 };
    ahb_req_t rq[8];
    printf("PCLK = HCLK/4, 통과형, PREADY 0. 앞에 IDLE k 개를 두고 APB 쓰기 1개\n");
    printf("  k   총 사이클   쓰기 주소단계 cycle   지연(주소단계→완료)\n");
    for (int k = 0; k <= 4; k++) {
        for (int i = 0; i < k; i++) rq[i] = IDLE();
        rq[k] = W(APB_BASE, 1);
        long c = sim_run(&cf, rq, k + 1, NULL, 0, NULL);
        printf("  %d   %6ld       %6d              %6ld\n", k, c, k, c - k);
    }
}

/* ---------- VCD ---------- */
typedef struct { const char *name; int width; char id; } vvar_t;
static const vvar_t VV[] = {
    { "HCLK", 1, '!' }, { "HTRANS", 2, '"' }, { "HADDR", 32, '#' }, { "HWRITE", 1, '$' },
    { "HWDATA", 32, '%' }, { "HRDATA", 32, '&' }, { "HREADY", 1, '\'' },
    { "PSEL", 1, '(' }, { "PENABLE", 1, ')' }, { "PADDR", 32, '*' }, { "PWRITE", 1, '+' },
    { "PWDATA", 32, ',' }, { "PRDATA", 32, '-' }, { "PREADY", 1, '.' },
};
#define NV (int)(sizeof VV / sizeof VV[0])

static void vbits(char *s, int width, uint32_t v, int valid) {   /* "b0101" 또는 "bx" */
    if (!valid) { strcpy(s, "bx"); return; }
    int k = 0;
    s[k++] = 'b';
    bool started = false;
    for (int i = width - 1; i >= 0; i--) {
        int bit = (int)((v >> i) & 1u);
        if (bit) started = true;
        if (started || i == 0) s[k++] = (char)('0' + bit);
    }
    s[k] = 0;
}
static void vval(char *s, int vi, const cyc_t *y, int clk) {
    const vvar_t *v = &VV[vi];
    uint32_t x = 0; int valid = 1;
    switch (vi) {
    case 0: x = (uint32_t)clk; break;
    case 1: x = y->htrans; break;
    case 2: x = y->haddr; valid = y->htrans != HT_IDLE; break;
    case 3: x = y->hwrite; valid = y->htrans != HT_IDLE; break;
    case 4: x = y->hwdata; valid = y->hwdata_v; break;
    case 5: x = y->hrdata; valid = y->hrdata_v; break;
    case 6: x = y->hready; break;
    case 7: x = y->psel; break;
    case 8: x = y->penable; break;
    case 9: x = y->paddr; valid = y->psel; break;
    case 10: x = y->pwrite; valid = y->psel; break;
    case 11: x = y->pwdata; valid = y->psel && y->pwrite; break;
    case 12: x = y->prdata; valid = y->prdata_v; break;
    case 13: x = y->pready; valid = y->psel && y->penable; break;
    }
    if (v->width == 1) { s[0] = valid ? (char)('0' + (x & 1u)) : 'x'; s[1] = v->id; s[2] = 0; return; }
    char b[40];
    vbits(b, v->width, x, valid);
    snprintf(s, 48, "%s %c", b, v->id);
}

static int demo_vcd(const char *path) {
    /* 장면: SRAM W → APB W(PREADY 1 wait) → APB R → SRAM R, PCLK = HCLK/2, 통과형 */
    mem_reset();
    g_apb[3] = 0xCAFE; g_sram[1] = 0x5A5A;
    bus_cfg_t cf = { 0, 1, 2, 0 };
    ahb_req_t rq[4] = { W(SRAM_BASE, 0x1111), W(APB_BASE + 0x8, 0x2222), R(APB_BASE + 0xC), R(SRAM_BASE + 4) };
    long nc = sim_run(&cf, rq, 4, d_w, 512, NULL);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    fprintf(f, "$date 2026-11-11 $end\n$version ARCH-M13 demo_bus (model, not RTL) $end\n$timescale 1ns $end\n");
    fprintf(f, "$scope module ahb_apb $end\n");
    for (int i = 0; i < NV; i++) fprintf(f, "$var wire %d %c %s $end\n", VV[i].width, VV[i].id, VV[i].name);
    fprintf(f, "$upscope $end\n$enddefinitions $end\n");
    char prev[NV][48], cur[48];
    memset(prev, 0, sizeof prev);
    long changes = 0;
    for (long c = 0; c < nc; c++) {
        for (int half = 0; half < 2; half++) {           /* HCLK 주기 10 ns: 상승 = 10c, 하강 = 10c+5 */
            fprintf(f, "#%ld\n", 10 * c + 5 * half);
            if (c == 0 && half == 0) fprintf(f, "$dumpvars\n");
            for (int i = 0; i < NV; i++) {
                vval(cur, i, &d_w[c], half == 0);
                if (strcmp(cur, prev[i]) != 0) { fprintf(f, "%s\n", cur); strcpy(prev[i], cur); changes++; }
            }
            if (c == 0 && half == 0) fprintf(f, "$end\n");
        }
    }
    fprintf(f, "#%ld\n", 10 * nc);
    fclose(f);
    printf("wrote %s: %ld cycles, %d signals, %ld value changes (Surfer / GTKWave 로 열기)\n", path, nc, NV, changes);
    wave_format(d_w, nc, 1, d_txt, sizeof d_txt);
    printf("%s", d_txt);
    return 0;
}

int main(int argc, char **argv) {
    const char *m = argc > 1 ? argv[1] : "all";
    bool all = strcmp(m, "all") == 0;
    if (all || !strcmp(m, "wave"))  { demo_wave(); }
    if (all || !strcmp(m, "table")) { demo_table(); printf("\n"); }
    if (all || !strcmp(m, "phase")) { demo_phase(); printf("\n"); }
    if (all || !strcmp(m, "vcd"))   return demo_vcd(argc > 2 ? argv[2] : "/tmp/esn_ARCH-M13/m13.vcd");
    return 0;
}
