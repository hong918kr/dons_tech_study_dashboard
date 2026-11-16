/* BM-A07 추가 연습 — 호스트 C (Mac: cc, VM: run_qemu.sh practice).
 * 입력 = run_qemu.sh su / solution 의 "실제 출력" 을 그대로 붙여 넣은 문자열 (펌웨어를 바꾸면 다시 붙여 넣는다).
 *   연습 1  .su 파서            연습 2  호출 그래프 최악 경로 (재귀·간접 호출·모르는 함수)
 *   연습 3  ISR 중첩 예산        연습 4  출시 체크리스트 판정       연습 5  HWM 스캔 방향과 구멍
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A07_pr && /tmp/BM-A07_pr
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; else printf("FAIL line %d: %s\n", __LINE__, #e); } while (0)

/* ---- run_qemu.sh su 실제 출력 (arm-none-eabi-gcc 14.2.1 -O2 -mcpu=cortex-m3) ---- */
static const char k_su[] =
    "frames.c:34:36:path_leaf\t64\tstatic\n"      "frames.c:51:36:path_mid\t136\tstatic\n"
    "frames.c:57:36:path_top\t264\tstatic\n"      "frames.c:64:38:irq_low_body\t48\tstatic\n"
    "frames.c:75:38:irq_high_body\t80\tstatic\n"  "frames.c:82:29:Irq14_Handler\t0\tstatic\n"
    "frames.c:83:29:Irq15_Handler\t0\tstatic\n"   "frames.c:86:36:recurse_until\t24\tstatic\n"
    "frames.c:97:36:big_sparse\t264\tstatic\n"    "frames.c:104:51:ssp_copy\t32\tstatic\n"
    "solution.c:28:13:sh_hex\t24\tstatic\n"       "solution.c:34:13:sh_dec\t24\tstatic\n"
    "solution.c:41:39:sh_exit\t8\tstatic\n"       "solution.c:161:12:final_report\t8\tstatic\n"
    "solution.c:181:29:HardFault_Handler\t0\tstatic\n" "solution.c:194:6:Default_Handler\t8\tstatic\n"
    "solution.c:133:6:SysTick_Handler\t4\tstatic\n"    "solution.c:92:32:stack_paint\t0\tstatic\n"
    "solution.c:99:46:stack_lowest_used\t0\tstatic\n"  "solution.c:146:43:measure_call\t16\tstatic\n"
    "solution.c:108:6:canary_arm\t0\tstatic\n"    "solution.c:111:5:canary_ok\t0\tstatic\n"
    "solution.c:169:32:__stack_chk_fail\t8\tstatic\n"  "solution.c:176:28:hardfault_report\t8\tstatic\n"
    "solution.c:211:5:main\t80\tstatic\n"         "solution.c:354:32:Reset_Handler\t8\tstatic\n";
static const char k_edges[] =
    "Default_Handler sh_exit\nHardFault_Handler hardfault_report\nIrq14_Handler irq_low_body\n"
    "Irq15_Handler irq_high_body\nReset_Handler main\nReset_Handler sh_exit\nReset_Handler stack_paint\n"
    "__stack_chk_fail final_report\n__stack_chk_fail sh_dec\n__stack_chk_fail sh_exit\nbig_sparse path_leaf\n"
    "final_report sh_dec\nhardfault_report sh_exit\nhardfault_report sh_hex\nmain final_report\n"
    "main measure_call\nmain recurse_until\nmain sh_dec\nmain sh_hex\nmain ssp_copy\nmain stack_lowest_used\n"
    "main stack_paint\nmeasure_call *INDIRECT*\nmeasure_call stack_lowest_used\nmeasure_call stack_paint\n"
    "path_mid path_leaf\npath_top path_mid\nrecurse_until *INDIRECT*\nrecurse_until recurse_until\n"
    "ssp_copy __stack_chk_fail\n";
#define MEASURED_HWM 768u          /* run_qemu.sh solution: "A5 worst normal HWM ... = 768 B" */
#define RESERVED     2048u
#define GUARD        32u

/* ================= 연습 1: .su 파서 ================= */
#define MAXF 64
typedef struct { char name[40]; uint32_t bytes; int dynamic; } su_t;
static su_t g_fn[MAXF];
static int g_nfn;
/* 한 줄 = "file:line:col:name<TAB>bytes<TAB>static|dynamic|dynamic,bounded". 반환: 읽은 개수, 형식 오류 줄은 *bad 에 셈 */
static int su_parse(const char *text, su_t *tab, int cap, int *bad) {
    int n = 0;
    *bad = 0;
    while (*text) {
        const char *eol = strchr(text, '\n');
        size_t len = eol ? (size_t)(eol - text) : strlen(text);
        char line[160], name[40], qual[24];
        unsigned bytes;
        if (len < sizeof line) {
            memcpy(line, text, len); line[len] = 0;
            char *tab1 = strchr(line, '\t'), *colon = NULL;
            if (tab1) { *tab1 = 0; colon = strrchr(line, ':'); }
            if (colon && sscanf(tab1 + 1, "%u\t%23s", &bytes, qual) == 2 && strlen(colon + 1) < sizeof name) {
                strcpy(name, colon + 1);
                if (n < cap) {
                    strcpy(tab[n].name, name); tab[n].bytes = bytes;
                    tab[n].dynamic = strncmp(qual, "dynamic", 7) == 0;
                    ++n;
                } else ++*bad;                   /* 용량 초과도 "못 읽음" 으로 보고 */
            } else ++*bad;
        } else ++*bad;
        text += len + (eol ? 1 : 0);
    }
    return n;
}
static int fn_index(const char *name) {
    for (int i = 0; i < g_nfn; ++i) if (!strcmp(g_fn[i].name, name)) return i;
    return -1;
}

/* ================= 연습 2: 호출 그래프 최악 경로 ================= */
#define MAXE 96
static int g_ec, g_ecallee[MAXE], g_ecaller[MAXE];      /* callee -1 = *INDIRECT* */
typedef struct { const char *caller; const char *targets[6]; } indirect_t;      /* 사람이 단 주석 */
typedef struct { const char *fn; uint32_t max_depth; } rbound_t;                 /* 재귀 깊이 주석 */
typedef struct { const char *caller, *callee; } skip_t;                          /* 시험 전용 간선 제외 */
static const indirect_t *g_ind; static int g_nind;
static const rbound_t *g_rb; static int g_nrb;
static const skip_t *g_skip; static int g_nskip;
#define UNBOUNDED 0xFFFFFFFFu
#define UNKNOWN   0xFFFFFFFEu
static uint32_t g_memo[MAXF];
static int g_state[MAXF];                                 /* 0 미방문, 1 방문 중(스택 위), 2 완료 */

static void edges_parse(const char *t) {
    char a[40], b[40];
    int k;
    g_ec = 0;
    while (sscanf(t, "%39s %39s%n", a, b, &k) == 2 && g_ec < MAXE) {
        g_ecaller[g_ec] = fn_index(a);
        g_ecallee[g_ec] = strcmp(b, "*INDIRECT*") ? fn_index(b) : -1;
        if (g_ecallee[g_ec] == -1 && strcmp(b, "*INDIRECT*")) g_ecallee[g_ec] = -2;   /* .su 없는 함수 */
        ++g_ec;
        t += k;
    }
}
static uint32_t worst(int f);
static uint32_t combine(uint32_t best, uint32_t w) {
    if (best == UNKNOWN || w == UNKNOWN) return UNKNOWN;
    if (best == UNBOUNDED || w == UNBOUNDED) return UNBOUNDED;
    return w > best ? w : best;
}
static int skipped(int caller, int callee) {
    for (int i = 0; i < g_nskip; ++i)
        if (fn_index(g_skip[i].caller) == caller && fn_index(g_skip[i].callee) == callee) return 1;
    return 0;
}
/* worst(f) = su(f) + max(worst(callee)). 재귀(자기/상호) → 깊이 주석이 있으면 depth·su(f) + 나머지, 없으면 UNBOUNDED.
 * 간접 호출 → 주석의 후보들 중 최댓값, 주석이 없으면 UNKNOWN. .su 없는 callee → UNKNOWN. */
static uint32_t worst(int f) {
    if (g_state[f] == 2) return g_memo[f];
    if (g_state[f] == 1) return UNBOUNDED;                  /* 순환 발견 */
    g_state[f] = 1;
    uint32_t best = 0, depth = 1;
    for (int i = 0; i < g_nrb; ++i) if (fn_index(g_rb[i].fn) == f) depth = g_rb[i].max_depth;
    for (int e = 0; e < g_ec; ++e) {
        if (g_ecaller[e] != f) continue;
        int c = g_ecallee[e];
        uint32_t w;
        if (c == f) {                                       /* 자기 재귀: 깊이 주석이 있으면 depth·su 로 처리 */
            int annotated = 0;
            for (int i = 0; i < g_nrb; ++i) if (fn_index(g_rb[i].fn) == f) annotated = 1;
            w = annotated ? 0u : UNBOUNDED;
        }
        else if (c == -2) w = UNKNOWN;
        else if (c == -1) {
            w = UNKNOWN;
            for (int i = 0; i < g_nind; ++i) {
                if (fn_index(g_ind[i].caller) != f) continue;
                w = 0;
                for (int t = 0; t < 6 && g_ind[i].targets[t]; ++t) {
                    int ti = fn_index(g_ind[i].targets[t]);
                    w = combine(w, ti < 0 ? UNKNOWN : worst(ti));
                }
            }
        } else if (skipped(f, c)) w = 0;
        else w = worst(c);
        best = combine(best, w);
    }
    uint32_t r = best >= UNKNOWN ? best : depth * g_fn[f].bytes + best;
    g_memo[f] = r; g_state[f] = 2;
    return r;
}
static uint32_t worst_of(const char *name) {
    memset(g_state, 0, sizeof g_state);
    int i = fn_index(name);
    return i < 0 ? UNKNOWN : worst(i);
}

/* ================= 연습 3: ISR 중첩 예산 ================= */
typedef struct { const char *isr; int prio; } isr_t;      /* prio: 작을수록 급함, HardFault = -1 */
/* 서로 다른 선점 우선순위마다 한 단계: 예외 프레임(32 B, FP 확장 104 B) + 정렬 패딩 4 B + 핸들러 최악 경로.
 * 같은 우선순위끼리는 서로 선점 못 하므로 그 단계에서는 최댓값 하나만. */
static uint32_t isr_budget(const isr_t *v, int n, int fp_frame) {
    uint32_t total = 0;
    for (int p = -1; p < 256; ++p) {
        uint32_t lvl = 0; int any = 0;
        for (int i = 0; i < n; ++i) if (v[i].prio == p) {
            uint32_t w = worst_of(v[i].isr);
            if (w > lvl) lvl = w;
            any = 1;
        }
        if (any) total += (fp_frame ? 104u : 32u) + 4u + lvl;
    }
    return total;
}

/* ================= 연습 5: HWM 스캔 방향 ================= */
#define PAINT 0xDEADBEEFu
static uint32_t xs = 0xA07u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }
static int scan_bottom_up(const uint32_t *s, int n) { int i = 0; while (i < n && s[i] == PAINT) ++i; return i; }
static int scan_top_down(const uint32_t *s, int n) { int i = n - 1; while (i >= 0 && s[i] != PAINT) --i; return i + 1; }

int main(void) {
    /* 연습 1 */
    int bad;
    g_nfn = su_parse(k_su, g_fn, MAXF, &bad);
    CHECK(g_nfn == 26 && bad == 0);
    CHECK(g_fn[fn_index("path_top")].bytes == 264u && g_fn[fn_index("main")].bytes == 80u);
    su_t small[4];
    CHECK(su_parse(k_su, small, 4, &bad) == 4 && bad == 22);                  /* 용량 초과 보고 */
    CHECK(su_parse("garbage line\nf.c:1:1:ok\t12\tdynamic,bounded\n", small, 4, &bad) == 1 && bad == 1 &&
          small[0].bytes == 12u && small[0].dynamic == 1);
    printf("ex1: %d functions parsed from .su\n", g_nfn);

    /* 연습 2 */
    edges_parse(k_edges);
    CHECK(g_ec == 30);
    CHECK(worst_of("path_top") == 464u && worst_of("big_sparse") == 328u && worst_of("ssp_copy") == 72u);
    CHECK(worst_of("measure_call") == UNKNOWN);                                /* 간접 호출, 주석 없음 */
    CHECK(worst_of("Reset_Handler") == UNKNOWN);
    static const indirect_t ind[] = {
        { "measure_call", { "path_leaf", "path_mid", "path_top", "big_sparse", 0 } },
        { "recurse_until", { "big_sparse", 0 } },
    };
    g_ind = ind; g_nind = 2;
    CHECK(worst_of("measure_call") == 16u + 464u);
    CHECK(worst_of("Reset_Handler") == UNBOUNDED);                             /* recurse_until 재귀 */
    static const rbound_t rb[] = { { "recurse_until", 86u } };                 /* B2 의 depth=85 → 86 프레임 */
    g_rb = rb; g_nrb = 1;
    uint32_t w_test = worst_of("Reset_Handler");
    CHECK(worst_of("recurse_until") == 86u * 24u + 328u);
    CHECK(w_test == 8u + 80u + 86u * 24u + 328u && w_test > RESERVED);         /* 시험 경로는 일부러 넘친다 */
    g_nrb = 0;
    static const skip_t skip[] = { { "main", "recurse_until" } };              /* 제품 빌드엔 없는 시험 경로 */
    g_skip = skip; g_nskip = 1;
    uint32_t w_main = worst_of("Reset_Handler");
    CHECK(w_main == 8u + 80u + 16u + 464u);
    printf("ex2: worst(Reset_Handler) test build (recursion<=86) = %u B > reserved; product = %u B "
           "(Reset 8 + main 80 + measure_call 16 + top/mid/leaf 464)\n", w_test, w_main);

    /* 연습 3 */
    static const isr_t isrs[] = { { "SysTick_Handler", 0xE0 }, { "Irq14_Handler", 0x80 },
                                  { "Irq15_Handler", 0x40 }, { "HardFault_Handler", -1 } };
    uint32_t b3 = isr_budget(isrs, 3, 0), b4 = isr_budget(isrs, 4, 0), bfp = isr_budget(isrs, 3, 1);
    CHECK(b3 == (32u + 4u + 4u) + (32u + 4u + 48u) + (32u + 4u + 80u));
    CHECK(b4 == b3 + 32u + 4u + 32u);
    CHECK(bfp == b3 + 3u * 72u);
    static const isr_t same[] = { { "Irq14_Handler", 0x80 }, { "Irq15_Handler", 0x80 } };
    CHECK(isr_budget(same, 2, 0) == 32u + 4u + 80u);                           /* 같은 우선순위 = 중첩 없음 */
    printf("ex3: ISR budget %u B (SysTick/IRQ14/IRQ15), +HardFault %u B, FP frames %u B\n", b3, b4, bfp);

    /* 연습 4: 체크리스트 */
    uint32_t stat = w_main + b4, maxframe = 0;
    for (int i = 0; i < g_nfn; ++i) if (g_fn[i].bytes > maxframe) maxframe = g_fn[i].bytes;
    int pass_static = stat * 4u <= RESERVED * 3u;       /* 정적 최악 ≤ 75 % */
    int pass_meas = MEASURED_HWM <= stat;               /* 실측 > 정적 이면 분석이 뭔가 놓쳤다 */
    int warn_guard = maxframe > GUARD;
    int margin_ok = 64u >= worst_of("stack_paint") + worst_of("stack_lowest_used") + 32u + 4u + worst_of("SysTick_Handler");
    printf("ex4: item                 value      rule                    verdict\n");
    printf("     reserved             %4u B\n", RESERVED);
    printf("     static worst         %4u B     <= 75%% of reserved      %s (%u%%)\n", stat,
           pass_static ? "PASS" : "FAIL", stat * 100u / RESERVED);
    printf("     measured HWM (QEMU)  %4u B     <= static worst         %s\n", MEASURED_HWM,
           pass_meas ? "PASS" : "FAIL");
    printf("     margin (static)      %4u B\n", RESERVED - stat);
    printf("     largest frame        %4u B     <= guard %u B          %s\n", maxframe, GUARD,
           warn_guard ? "WARN (jump-over)" : "PASS");
    printf("     PAINT_MARGIN 64 B    covers helpers + SysTick frame  %s\n", margin_ok ? "PASS" : "FAIL");
    CHECK(stat == 876u && pass_static && pass_meas && warn_guard && maxframe == 264u && margin_ok);

    /* 연습 5: 구멍 있는 프레임에서 아래→위 스캔만 맞다 */
    uint32_t s[64];
    int wrong_td = 0, ok_bu = 0, trials = 5000;
    for (int t = 0; t < trials; ++t) {
        int lowest = (int)(xorshift32() % 64u);
        for (int i = 0; i < 64; ++i) s[i] = PAINT;
        for (int i = lowest; i < 64; ++i) s[i] = (i == lowest || xorshift32() % 5u) ? 0x20000000u + (uint32_t)i : PAINT;
        ok_bu += scan_bottom_up(s, 64) == lowest;
        wrong_td += scan_top_down(s, 64) != lowest;
    }
    CHECK(ok_bu == trials);
    CHECK(wrong_td > 0);
    for (int i = 0; i < 64; ++i) s[i] = PAINT;
    CHECK(scan_bottom_up(s, 64) == 64);                                         /* 전부 칠해짐 = 사용 0 */
    for (int i = 10; i < 64; ++i) s[i] = 7u;                                    /* 워드 10..63 사용 중인데 */
    s[10] = PAINT;                                                              /* 맨 아래 데이터가 우연히 PAINT 값 */
    CHECK(scan_bottom_up(s, 64) == 11);                                         /* → 한 워드(4 B) 과소 보고 */
    printf("ex5: %d random holey stacks: bottom-up exact %d/%d, top-down wrong %d times\n",
           trials, ok_bu, trials, wrong_td);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
