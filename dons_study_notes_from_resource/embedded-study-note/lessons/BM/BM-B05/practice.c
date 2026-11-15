/* BM-B05 추가 연습 — 같은 캡처 파일(레슨 폴더)로 4 문제를 한 번에 검증
 *   연습 1  size -B 를 size -A 에서 재현 (Berkeley 분류: .rodata 는 text, heap/stack 예약은 bss)
 *   연습 2  Reset_Handler 가 하는 일의 양: map 심볼 _sdata/_edata/_sbss/_ebss → 복사·제로화 바이트
 *   연습 3  주소 → 심볼 (HardFault 의 PC / BFAR 가 가리키는 함수·변수 찾기)
 *   연습 4  링크 전에 예측: rx_buf 를 얼마까지 키울 수 있나 (정렬 패딩 포함), ld 결과와 대조
 * 빌드 (레슨 폴더에서): cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-B05_pr && /tmp/BM-B05_pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ---- solution.c 에서 가져온 도우미·파서 (그대로) ---- */
static char g_map_good[16384], g_map_bad[16384];
static char g_sa_good[2048], g_sa_bad[2048], g_sb_good[512], g_sb_bad[512], g_ld_ovf[1024];
static bool read_file(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s (레슨 폴더에서 실행하세요)\n", path); return false; }
    size_t n = fread(buf, 1, cap - 1, f);
    bool ok = feof(f) != 0;              /* cap 보다 크면 잘린 것 → 실패 처리 */
    fclose(f);
    buf[n] = '\0';
    return ok;
}

static bool next_line(const char **p, char *line, size_t cap) {
    if (**p == '\0') return false;
    size_t i = 0;
    while (**p != '\0' && **p != '\n') {
        if (i + 1 < cap) line[i++] = **p;
        (*p)++;
    }
    if (**p == '\n') (*p)++;
    line[i] = '\0';
    return true;
}

static bool parse_ld_mem(const char *txt, const char *region, uint32_t *used) {
    const char *p = txt; char line[200], name[32], unit[8]; unsigned long v;
    while (next_line(&p, line, sizeof line)) {
        if (sscanf(line, " %31[^:]: %lu %7s", name, &v, unit) == 3 && strcmp(name, region) == 0) {
            uint32_t mul = strcmp(unit, "KB") == 0 ? 1024u : strcmp(unit, "MB") == 0 ? 1048576u : 1u;
            *used = (uint32_t)v * mul;
            return true;
        }
    }
    return false;
}

static bool parse_ld_overflow(const char *txt, const char *region, uint32_t *by) {
    char key[64];
    snprintf(key, sizeof key, "region `%s' overflowed by ", region);
    const char *s = strstr(txt, key);
    if (!s) return false;
    *by = (uint32_t)strtoul(s + strlen(key), NULL, 10);
    return true;
}

typedef struct { char name[32]; uint32_t size, addr; } sec_t;
typedef struct { uint32_t text, data, bss, dec; } berk_t;
typedef struct { char kind[16]; char sym[40]; uint32_t addr, size; } isec_t;

static int parse_size_A(const char *txt, sec_t *out, int cap) {
    const char *p = txt; char line[200]; int n = 0; bool hdr = false;
    while (next_line(&p, line, sizeof line)) {
        if (!hdr) { hdr = strncmp(line, "section", 7) == 0; continue; }
        if (strncmp(line, "Total", 5) == 0) return n;
        char name[64], sz[24], ad[24];
        if (sscanf(line, "%63s %23s %23s", name, sz, ad) != 3) continue;
        if (n >= cap) return -1;
        snprintf(out[n].name, sizeof out[n].name, "%s", name);
        out[n].size = (uint32_t)strtoul(sz, NULL, 0);
        out[n].addr = (uint32_t)strtoul(ad, NULL, 0);
        n++;
    }
    return -1;
}

static bool parse_size_B(const char *txt, berk_t *b) {
    const char *p = txt; char line[200];
    while (next_line(&p, line, sizeof line)) {
        unsigned long t, d, s, dec;
        if (sscanf(line, " %lu %lu %lu %lu", &t, &d, &s, &dec) == 4) {
            b->text = (uint32_t)t; b->data = (uint32_t)d; b->bss = (uint32_t)s; b->dec = (uint32_t)dec;
            return true;
        }
    }
    return false;
}

static bool map_symbol(const char *map, const char *sym, uint32_t *val) {
    const char *p = map; char line[200];
    while (next_line(&p, line, sizeof line)) {
        char a[24], name[64];
        if (line[0] != ' ' || sscanf(line, " %23s %63s", a, name) != 2) continue;
        if (strncmp(a, "0x", 2) != 0 || strcmp(name, sym) != 0) continue;
        *val = (uint32_t)strtoul(a, NULL, 16);
        return true;
    }
    return false;
}

static bool in_region(uint32_t a, uint32_t org, uint32_t len) { return a >= org && a - org < len; }

static bool has_prefix(const char *s, const char *pre) { return strncmp(s, pre, strlen(pre)) == 0; }

static int parse_map_inputs(const char *map, isec_t *out, int cap) {
    const char *p = strstr(map, "Linker script and memory map");
    if (!p) return 0;
    char line[200], nxt[200]; int n = 0;
    while (next_line(&p, line, sizeof line)) {
        if (line[0] != ' ' || line[1] != '.') continue;
        char name[64]; unsigned long a, sz; int used = 0;
        if (sscanf(line, " %63s%n", name, &used) != 1) continue;
        if (sscanf(line + used, " 0x%lx 0x%lx", &a, &sz) != 2) {         /* 줄바꿈된 형태 */
            if (!next_line(&p, nxt, sizeof nxt) || sscanf(nxt, " 0x%lx 0x%lx", &a, &sz) != 2) continue;
        }
        if (sz == 0u) continue;
        if (n >= cap) return -1;
        const char *dot2 = strchr(name + 1, '.');
        if (dot2) {
            snprintf(out[n].kind, sizeof out[n].kind, "%.*s", (int)(dot2 - name), name);
            snprintf(out[n].sym, sizeof out[n].sym, "%s", dot2 + 1);
        } else {
            snprintf(out[n].kind, sizeof out[n].kind, "%s", name);
            snprintf(out[n].sym, sizeof out[n].sym, "%s", name);
        }
        out[n].addr = (uint32_t)a; out[n].size = (uint32_t)sz;
        n++;
    }
    return n;
}

#define FLASH_ORG 0x08000000u
#define FLASH_LEN (512u * 1024u)
#define RAM_ORG   0x20000000u
#define RAM_LEN   (128u * 1024u)

/* ---- 연습 1: Berkeley (size -B) 를 size -A 에서 ----
 * text = Flash 에 있는 할당 섹션 (코드 + .rodata + 벡터)
 * data = RAM 에 있고 내용이 있는 섹션 (.data)
 * bss  = RAM 에 있고 내용이 없는 섹션 (.bss + ._user_heap_stack — 예약도 bss 로 잡힌다) */
static berk_t berk_from_sections(const sec_t *s, int n) {
    berk_t b = {0, 0, 0, 0};
    for (int i = 0; i < n; i++) {
        if (s[i].addr == 0u) continue;
        if (in_region(s[i].addr, FLASH_ORG, FLASH_LEN)) b.text += s[i].size;
        else if (has_prefix(s[i].name, ".bss") || has_prefix(s[i].name, "._user_heap_stack")) b.bss += s[i].size;
        else b.data += s[i].size;
    }
    b.dec = b.text + b.data + b.bss;
    return b;
}

/* ---- 연습 2: startup 이 복사·제로화하는 양 ---- */
typedef struct { uint32_t copy_bytes, zero_bytes, lma; } startup_t;
static bool startup_work(const char *map, startup_t *st) {
    uint32_t sd, ed, sb, eb, li;
    if (!map_symbol(map, "_sdata", &sd) || !map_symbol(map, "_edata", &ed) || !map_symbol(map, "_sbss", &sb) ||
        !map_symbol(map, "_ebss", &eb) || !map_symbol(map, "_sidata", &li)) return false;
    st->copy_bytes = ed - sd; st->zero_bytes = eb - sb; st->lma = li;
    return true;
}

/* ---- 연습 3: 주소 → 입력 섹션(심볼) + 오프셋. 어느 섹션에도 없으면(채움·예약) NULL ---- */
static const isec_t *addr2sym(const isec_t *s, int n, uint32_t addr, uint32_t *off) {
    for (int i = 0; i < n; i++)
        if (addr >= s[i].addr && addr - s[i].addr < s[i].size) { *off = addr - s[i].addr; return &s[i]; }
    return NULL;
}

/* ---- 연습 4: fw.ld 의 정렬 규칙을 그대로 따라 RAM 사용량 예측 ----
 * .bss 끝 ALIGN(4) → ._user_heap_stack 시작 ALIGN(8) → + heap + stack → ALIGN(8). 사용량 = 끝 - RAM 시작 */
static uint32_t al(uint32_t x, uint32_t a) { return (x + a - 1u) & ~(a - 1u); }
static uint32_t ram_used_pred(uint32_t sbss, uint32_t bss, uint32_t heap, uint32_t stack) {
    uint32_t end = al(sbss + bss, 4u);
    return al(al(end, 8u) + heap + stack, 8u) - RAM_ORG;
}
/* rx_buf(.bss 의 마지막 객체) 크기를 rx 로 바꿨을 때 들어가는 최대 rx */
static uint32_t rx_max(uint32_t sbss, uint32_t bss_now, uint32_t rx_now, uint32_t heap, uint32_t stack) {
    uint32_t base = bss_now - rx_now;              /* rx_buf 를 뺀 .bss */
    uint32_t hi = RAM_LEN;                          /* 이분 탐색: used(rx) 는 rx 에 단조 증가 */
    uint32_t lo = 0u;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo + 1u) / 2u;
        if (ram_used_pred(sbss, base + mid, heap, stack) <= RAM_LEN) lo = mid; else hi = mid - 1u;
    }
    return lo;
}

static const sec_t *find_sec(const sec_t *s, int n, const char *name) {
    for (int i = 0; i < n; i++) if (strcmp(s[i].name, name) == 0) return &s[i];
    return NULL;
}

int main(void) {
    if (!read_file("fw_good.map", g_map_good, sizeof g_map_good) || !read_file("fw_bad.map", g_map_bad, sizeof g_map_bad) ||
        !read_file("size_A_good.txt", g_sa_good, sizeof g_sa_good) || !read_file("size_A_bad.txt", g_sa_bad, sizeof g_sa_bad) ||
        !read_file("size_B_good.txt", g_sb_good, sizeof g_sb_good) || !read_file("size_B_bad.txt", g_sb_bad, sizeof g_sb_bad) ||
        !read_file("ldmem_ovf.txt", g_ld_ovf, sizeof g_ld_ovf)) return 1;
    static sec_t sg[32], sb[32];
    int ng = parse_size_A(g_sa_good, sg, 32), nb = parse_size_A(g_sa_bad, sb, 32);
    CHECK(ng == 16 && nb == 16);

    /* 연습 1 */
    berk_t rg = {0, 0, 0, 0}, rb = {0, 0, 0, 0}, cg = berk_from_sections(sg, ng), cb = berk_from_sections(sb, nb);
    CHECK(parse_size_B(g_sb_good, &rg) && parse_size_B(g_sb_bad, &rb));
    CHECK(cg.text == rg.text && cg.data == rg.data && cg.bss == rg.bss && cg.dec == rg.dec);
    CHECK(cb.text == rb.text && cb.data == rb.data && cb.bss == rb.bss && cb.dec == rb.dec);
    CHECK(rg.dec - 4u == rb.dec);                    /* dec 는 거의 같다: size -B 만 보면 버그가 안 보인다 */
    CHECK(rg.text + rg.data == rb.text + rb.data);   /* Flash 는 같고 */
    CHECK(rb.data + rb.bss - (rg.data + rg.bss) == 8208u);   /* RAM 만 8208 B 늘었다 */

    /* 연습 2 */
    startup_t g, b;
    CHECK(startup_work(g_map_good, &g) && g.copy_bytes == 28u && g.zero_bytes == 3080u && g.lma == 0x08002180u);
    CHECK(startup_work(g_map_bad, &b) && b.copy_bytes == 8240u && b.zero_bytes == 3080u && b.lma == 0x0800016cu);
    const sec_t *dg = find_sec(sg, ng, ".data");
    CHECK(dg && dg->size == g.copy_bytes);
    CHECK(b.copy_bytes / 4u == 2060u && g.copy_bytes / 4u == 7u);  /* 복사 루프 반복 횟수 (ldr/str/cmp/bne 4 명령) */

    /* 연습 3 */
    static isec_t ig[64], ib[64];
    int mg = parse_map_inputs(g_map_good, ig, 64), mb = parse_map_inputs(g_map_bad, ib, 64);
    uint32_t off = 0;
    const isec_t *h = addr2sym(ig, mg, 0x08000100u, &off);
    CHECK(h && strcmp(h->sym, "Reset_Handler") == 0 && off == 0x20u);
    h = addr2sym(ib, mb, 0x20000040u, &off);
    CHECK(h && strcmp(h->sym, "sine_lut") == 0 && off == 0x10u);        /* bad 빌드: RAM 주소가 테이블 */
    h = addr2sym(ig, mg, 0x20000040u, &off);
    CHECK(h && strcmp(h->sym, "g_dac_dma_buf") == 0 && off == 0x1cu);   /* good 빌드: 같은 주소, 다른 변수 */
    CHECK(addr2sym(ig, mg, 0x0800016au, &off) == NULL);                 /* *fill* 2 바이트 */
    CHECK(addr2sym(ig, mg, 0x20001000u, &off) == NULL);                 /* heap 예약 (입력 섹션 없음) */

    /* 연습 4 */
    uint32_t sbss = 0, ebss = 0, heap = 0, stack = 0, ovf_used = 0, ovf_by = 0;
    CHECK(map_symbol(g_map_good, "_sbss", &sbss) && map_symbol(g_map_good, "_ebss", &ebss) &&
          map_symbol(g_map_good, "_Min_Heap_Size", &heap) && map_symbol(g_map_good, "_Min_Stack_Size", &stack));
    uint32_t bss = ebss - sbss;
    CHECK(ram_used_pred(sbss, bss, heap, stack) == 9256u);                               /* ld: 9256 B */
    CHECK(parse_ld_mem(g_ld_ovf, "RAM", &ovf_used) && parse_ld_overflow(g_ld_ovf, "RAM", &ovf_by));
    CHECK(ram_used_pred(sbss, bss - 1024u + 122880u, heap, stack) == ovf_used);          /* 131112 */
    CHECK(ovf_used - RAM_LEN == ovf_by);                                                 /* 40 */
    uint32_t mx = rx_max(sbss, bss, 1024u, heap, stack);
    CHECK(mx == 122844u);                                       /* VM 에서 링크 확인: 100.00 %, exit 0 */
    CHECK(ram_used_pred(sbss, bss - 1024u + mx + 1u, heap, stack) - RAM_LEN == 8u);   /* +1 B → ld "by 8 bytes" */

    printf("berkeley(-A) good: text %u data %u bss %u | bad: text %u data %u bss %u\n",
           (unsigned)cg.text, (unsigned)cg.data, (unsigned)cg.bss, (unsigned)cb.text, (unsigned)cb.data, (unsigned)cb.bss);
    printf("startup copy: good %u B (%u words), bad %u B (%u words); zero %u B\n", (unsigned)g.copy_bytes,
           (unsigned)(g.copy_bytes / 4u), (unsigned)b.copy_bytes, (unsigned)(b.copy_bytes / 4u), (unsigned)g.zero_bytes);
    printf("rx_buf max = %u B (now 1024)\n", (unsigned)mx);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
