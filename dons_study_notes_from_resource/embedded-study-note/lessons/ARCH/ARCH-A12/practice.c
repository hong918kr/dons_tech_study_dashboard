/* ARCH-A12 추가 연습 — 한 번에 검증
 *
 * solution.c 의 맵 모델·검사기를 그대로 가져와(main 만 이름을 바꿔 포함) 4 개 연습의 답을 검사한다.
 *   연습 1  v1 → rev2 맵 diff: 소프트웨어(디바이스 트리·드라이버)가 알아야 할 변경 목록 + "같은 PA, 다른 뜻" 주소
 *   연습 2  상위 비트 비교 디코더: POW2 영역은 (mask, match) 한 쌍, 아닌 영역은 정렬된 2^k 블록 몇 개로 쪼개지나
 *   연습 3  DRAM 대역폭 예산 (대표값/가정): v1 과 rev2(NPU 2 개)의 평균 사용률
 *   연습 4  맵 데이터 → C 헤더 #define 생성 (single source of truth), 이름 중복·식별자 검사
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#define main solution_main
#include "solution.c"
#undef main

#include <ctype.h>

/* ---------- 연습 1: 맵 diff ---------- */
enum { CH_NEW = 1, CH_REMOVED = 2, CH_MOVED = 4, CH_RESIZED = 8, CH_ATTR = 16, CH_MASTERS = 32 };
typedef struct { const char *name; unsigned what; } change_t;

static int map_diff(const soc_t *a, const soc_t *b, change_t *out) {
    int n = 0;
    for (int i = 0; i < a->n; i++) {
        int j = find_name(b, a->r[i].name);
        unsigned w = 0;
        if (j < 0) w = CH_REMOVED;
        else {
            if (a->r[i].base != b->r[j].base) w |= CH_MOVED;
            if (a->r[i].size != b->r[j].size) w |= CH_RESIZED;
            if (a->r[i].attr != b->r[j].attr) w |= CH_ATTR;
            if (a->r[i].masters != b->r[j].masters) w |= CH_MASTERS;
        }
        if (w) { out[n].name = a->r[i].name; out[n].what = w; n++; }
    }
    for (int j = 0; j < b->n; j++)
        if (find_name(a, b->r[j].name) < 0) { out[n].name = b->r[j].name; out[n].what = CH_NEW; n++; }
    return n;
}

/* ---------- 연습 2: 정렬된 2^k 블록 분해 (CIDR 같은 탐욕법) ---------- */
typedef struct { uint64_t base, size; } blk_t;
static int pow2_split(uint64_t base, uint64_t size, blk_t *out, int max) {
    int n = 0;
    uint64_t cur = base, end = base + size;
    while (cur < end) {
        uint64_t blk = cur ? (cur & (~cur + 1)) : ((uint64_t)1 << 62);   /* cur 의 정렬 = 가장 낮은 1 비트 */
        while (blk > end - cur) blk >>= 1;                               /* 남은 길이 안에 들어갈 때까지 */
        if (n < max) { out[n].base = cur; out[n].size = blk; }
        n++;
        cur += blk;
    }
    return n;
}
static int log2u64(uint64_t x) { int k = 0; while (x > 1) { x >>= 1; k++; } return k; }

/* ---------- 연습 3: 대역폭 예산 ---------- */
typedef struct { const char *what; double mbps; } flow_t;   /* MB/s = 10^6 B/s */

static double sum_flows(const flow_t *f, int n) { double s = 0; for (int i = 0; i < n; i++) s += f[i].mbps; return s; }

/* ---------- 연습 4: 헤더 생성 ---------- */
static int gen_header(const soc_t *s, char *buf, size_t cap) {
    size_t off = 0;
    int idx[MAXR], k = map_sorted(s, idx);
    for (int t = 0; t < k && off < cap; t++) {
        const region_t *r = &s->r[idx[t]];
        int w = snprintf(buf + off, cap - off, "#define SOC_%s_BASE 0x%010llxULL\n#define SOC_%s_SIZE 0x%llxULL\n",
                         r->name, (unsigned long long)r->base, r->name, (unsigned long long)r->size);
        if (w < 0) return -1;
        off += (size_t)w;
    }
    return off < cap ? (int)off : -1;
}
static bool c_ident_upper(const char *s) {
    if (!(isupper((unsigned char)s[0]) || s[0] == '_')) return false;
    for (const char *p = s; *p; p++) if (!(isupper((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '_')) return false;
    return true;
}

int main(void) {
    char b1[24], b2[24];

    printf("== 연습 1: v1 -> rev2 diff ==\n");
    soc_t v1 = make_v1(), r2 = make_rev2_fixed();
    change_t ch[MAXR * 2];
    int nc = map_diff(&v1, &r2, ch);
    for (int i = 0; i < nc; i++)
        printf("  %-12s%s%s%s%s%s%s\n", ch[i].name, ch[i].what & CH_NEW ? " NEW" : "", ch[i].what & CH_REMOVED ? " REMOVED" : "",
               ch[i].what & CH_MOVED ? " MOVED" : "", ch[i].what & CH_RESIZED ? " RESIZED" : "",
               ch[i].what & CH_ATTR ? " ATTR" : "", ch[i].what & CH_MASTERS ? " MASTERS" : "");
    CHECK(nc == 5);
    unsigned w_main = 0, w_sh = 0, w_sec = 0, w_sys = 0, w_npu1 = 0;
    for (int i = 0; i < nc; i++) {
        if (!strcmp(ch[i].name, "DRAM_MAIN")) w_main = ch[i].what;
        if (!strcmp(ch[i].name, "DRAM_SHARED")) w_sh = ch[i].what;
        if (!strcmp(ch[i].name, "DRAM_SEC")) w_sec = ch[i].what;
        if (!strcmp(ch[i].name, "SYS_SRAM")) w_sys = ch[i].what;
        if (!strcmp(ch[i].name, "NPU1_CSR")) w_npu1 = ch[i].what;
    }
    CHECK(w_main == (CH_MOVED | CH_RESIZED) && w_sh == (CH_MOVED | CH_RESIZED | CH_MASTERS));
    CHECK(w_sec == CH_MOVED && w_sys == CH_MASTERS && w_npu1 == CH_NEW);
    /* 같은 PA 가 다른 영역이 된 주소: 각 영역 경계에서 CPU secure 읽기로 비교 */
    int same_pa_diff = 0;
    for (int i = 0; i < v1.n; i++) {
        uint64_t a = v1.r[i].base;
        dec_t x = decode(&v1, a, M_CPUB, true, false), y = decode(&r2, a, M_CPUB, true, false);
        const char *nx = x.region >= 0 ? v1.r[x.region].name : "-", *ny = y.region >= 0 ? r2.r[y.region].name : "-";
        if (strcmp(nx, ny)) { printf("  PA %s: v1 %s -> rev2 %s\n", fmt_pa(b1, a), nx, ny); same_pa_diff++; }
    }
    CHECK(same_pa_diff == 3);

    printf("== 연습 2: top-bits decoder ==\n");
    int bad = 0, pow2_regions = 0;
    for (int i = 0; i < v1.n; i++) {
        const region_t *r = &v1.r[i];
        if (!(r->attr & A_POW2) || r->parent) continue;
        pow2_regions++;
        uint64_t mask = (((uint64_t)1 << v1.pa_bits) - 1) & ~(r->size - 1);
        printf("  %-10s compare PA[39:%d] (%2d bits) == 0x%llx\n", r->name, log2u64(r->size), 40 - log2u64(r->size),
               (unsigned long long)(r->base >> log2u64(r->size)));
        for (int k = 0; k < 20000; k++) {
            uint64_t a = (k & 1) ? (((uint64_t)xorshift32() << 8) ^ xorshift32()) & (((uint64_t)1 << 40) - 1)
                                 : r->base + (xorshift32() % (uint32_t)(r->size > MB(64) ? MB(64) : r->size * 2)) - r->size / 2;
            bool by_mask = (a & mask) == r->base, by_range = a - r->base < r->size;
            if (by_mask != by_range) bad++;
        }
    }
    printf("  %d POW2 top-level regions, mask-vs-range mismatches %d\n", pow2_regions, bad);
    CHECK(pow2_regions == 11 && bad == 0);
    const char *non_pow2[2] = { "DRAM_MAIN", "DRAM_SHARED" };
    soc_t *maps[2] = { &v1, &r2 };
    int nblk_expect[2][2] = { { 2, 4 }, { 7, 1 } };
    for (int m = 0; m < 2; m++)
        for (int q = 0; q < 2; q++) {
            const region_t *r = &maps[m]->r[find_name(maps[m], non_pow2[q])];
            blk_t blk[16];
            int nb = pow2_split(r->base, r->size, blk, 16);
            uint64_t cur = r->base;
            bool ok = nb <= 16;
            for (int t = 0; t < nb && t < 16; t++) {
                ok = ok && blk[t].base == cur && is_pow2(blk[t].size) && blk[t].base % blk[t].size == 0;
                cur += blk[t].size;
            }
            ok = ok && cur == rend(r);
            printf("  %s %-11s %s +%s -> %d blocks:", m ? "rev2" : "v1  ", r->name, fmt_pa(b1, r->base), fmt_size(b2, r->size), nb);
            for (int t = 0; t < nb && t < 16; t++) printf(" %s", fmt_size(b2, blk[t].size));
            printf("\n");
            CHECK(ok && nb == nblk_expect[m][q]);
        }

    printf("== 연습 3: DRAM bandwidth budget (대표값/가정) ==\n");
    const double px4k = 3840.0 * 2160.0, fps = 30.0;
    flow_t v1f[] = {
        { "CSI-2 RAW10 dump (write)",  px4k * 1.25 * fps / 1e6 },
        { "ISP reads RAW10",           px4k * 1.25 * fps / 1e6 },
        { "ISP YUV420 out (write)",    px4k * 1.5 * fps / 1e6 },
        { "ISP 640x640 RGB (write)",   640.0 * 640.0 * 3.0 * fps / 1e6 },
        { "NPU0 input read",           640.0 * 640.0 * 3.0 * fps / 1e6 },
        { "NPU0 weights 6MB/inf",      6.0 * fps },
        { "NPU0 act. spill 24MB/inf",  24.0 * fps },
        { "CPU clusters",              1000.0 },
    };
    int n1 = (int)(sizeof v1f / sizeof v1f[0]);
    const double peak = 4266e6 * 4 / 1e6, eff = 0.70;      /* LPDDR4X-4266, x32 → MB/s; 효율 70% 가정 */
    for (int i = 0; i < n1; i++) printf("  %-28s %8.1f MB/s\n", v1f[i].what, v1f[i].mbps);
    double t1 = sum_flows(v1f, n1);
    double npu = v1f[4].mbps + v1f[5].mbps + v1f[6].mbps + v1f[3].mbps;   /* NPU1 이 같은 일을 하나 더 */
    double t2 = t1 + npu;
    printf("  peak %.1f MB/s, usable(70%%) %.1f MB/s\n", peak, peak * eff);
    printf("  v1   total %7.1f MB/s = %4.1f%% of usable\n", t1, 100 * t1 / (peak * eff));
    printf("  rev2 total %7.1f MB/s = %4.1f%% of usable (NPU1 adds %.1f)\n", t2, 100 * t2 / (peak * eff), npu);
    CHECK(peak > 17063.9 && peak < 17064.1);
    CHECK(v1f[2].mbps > 373.24 && v1f[2].mbps < 373.25);
    CHECK(t1 / (peak * eff) < 0.5 && t2 / (peak * eff) < 0.5);   /* 설계 규칙(가정): 평균 사용률 50% 이하 */

    printf("== 연습 4: header generation ==\n");
    static char hdr[8192];
    int len = gen_header(&r2, hdr, sizeof hdr);
    CHECK(len > 0);
    CHECK(strstr(hdr, "#define SOC_UART0_BASE 0x0010000000ULL\n") != NULL);
    CHECK(strstr(hdr, "#define SOC_DRAM_SEC_BASE 0x027c000000ULL\n") != NULL);
    CHECK(strstr(hdr, "#define SOC_DRAM_MAIN_SIZE 0x1bc000000ULL\n") != NULL);
    int dup = 0, badid = 0;
    for (int i = 0; i < r2.n; i++) {
        badid += !c_ident_upper(r2.r[i].name);
        for (int j = i + 1; j < r2.n; j++) dup += !strcmp(r2.r[i].name, r2.r[j].name);
    }
    CHECK(dup == 0 && badid == 0);
    printf("  %d bytes, first lines:\n", len);
    for (int i = 0, lines = 0; i < len && lines < 4; i++) { putchar(hdr[i]); if (hdr[i] == '\n') lines++; }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
