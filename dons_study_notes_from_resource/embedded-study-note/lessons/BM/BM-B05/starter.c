/* BM-B05 MCU 메모리 맵과 섹션 배치: map 파일 읽기
 *
 * 입력 = Lima VM 에서 진짜로 빌드한 펌웨어(fw.c + fw.ld, arm-none-eabi-gcc 14.2.1, Cortex-M4)의 캡처 파일:
 *   size_A_{good,bad}.txt  (arm-none-eabi-size -A -x)   size_B_{good,bad}.txt (size -B)
 *   fw_{good,bad}.map      (-Wl,-Map, 메모리 맵 부분)    ldmem_{good,bad,ovf}.txt (-Wl,--print-memory-usage)
 * Part A — size -A 를 읽어 Flash/RAM 예산표 (Flash = text + rodata + data(LMA), RAM = data + bss + heap + stack)
 * Part B — map 의 입력 섹션(-fdata-sections)을 읽어 good/bad 두 빌드를 diff → "const 빠진 테이블" 찾기
 * Part C — CI 예산 게이트: 부품(512K/128K, 64K/20K, 32K/8K)별 초과·경고 판정, ld overflow 메시지와 대조
 *
 * 빌드 (데이터 파일을 읽으므로 레슨 폴더에서 실행):
 *   cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-B05_st && /tmp/BM-B05_st   (solution.c 도 같은 식)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ======================= 주어진 도우미 (수정 불필요) ======================= */
#define FILE_CAP 16384
static char g_map_good[FILE_CAP], g_map_bad[FILE_CAP];
static char g_sa_good[2048], g_sa_bad[2048], g_sb_good[512], g_sb_bad[512];
static char g_ld_good[512], g_ld_bad[512], g_ld_ovf[1024];

static bool read_file(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s (레슨 폴더에서 실행하세요)\n", path); return false; }
    size_t n = fread(buf, 1, cap - 1, f);
    bool ok = feof(f) != 0;              /* cap 보다 크면 잘린 것 → 실패 처리 */
    fclose(f);
    buf[n] = '\0';
    return ok;
}

/* *p 에서 한 줄을 line 으로 꺼낸다 (개행 제외, 너무 긴 줄은 잘림). 끝이면 false. */
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

/* ld --print-memory-usage 의 "  RAM:  9256 B  128 KB  7.06%" 줄에서 사용 바이트 */
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

/* ld 에러 "region `RAM' overflowed by 40 bytes" 의 숫자 */
static bool parse_ld_overflow(const char *txt, const char *region, uint32_t *by) {
    char key[64];
    snprintf(key, sizeof key, "region `%s' overflowed by ", region);
    const char *s = strstr(txt, key);
    if (!s) return false;
    *by = (uint32_t)strtoul(s + strlen(key), NULL, 10);
    return true;
}

/* ======================= 자료 구조 ======================= */
typedef struct { char name[32]; uint32_t size, addr; } sec_t;          /* size -A 한 줄 */
typedef struct { uint32_t text, data, bss, dec; } berk_t;               /* size -B */
typedef struct { const char *name; uint32_t flash_org, flash_len, ram_org, ram_len; } part_t;
typedef struct {
    uint32_t text, rodata, data, bss;      /* data = RAM(VMA) 크기 = Flash(LMA) 이미지 크기 */
    uint32_t heap, stack, hs_pad;          /* ._user_heap_stack = heap + stack + 정렬 패딩 */
    uint32_t flash_total, ram_total;
    uint32_t flash_pct_x100, ram_pct_x100; /* 1.64 % → 164 (버림, ld 출력과 같은 자릿수) */
} budget_t;
typedef struct { char kind[16]; char sym[40]; uint32_t addr, size; } isec_t; /* map 입력 섹션 */
typedef struct { char sym[40]; char from[16]; char to[16]; uint32_t size; int32_t ram_delta; } move_t;

enum { GATE_FLASH_OVER = 1u, GATE_RAM_OVER = 2u, GATE_FLASH_WARN = 4u, GATE_RAM_WARN = 8u };

/* ======================= Part A: size 출력 → 예산표 ======================= */

/* TODO A1: `size -A -x` 출력을 파싱한다. "section size addr" 헤더 다음 줄부터 "Total" 줄 전까지.
 * 숫자는 0x 16진/10진 모두 허용 (strtoul base 0). 반환: 섹션 수 (디버그 섹션 포함),
 * "Total" 이 없으면(잘린 캡처) 또는 cap 초과면 -1. */
static int parse_size_A(const char *txt, sec_t *out, int cap) {
    (void)txt; (void)out; (void)cap;
    return -1;                          /* TODO A1 */
}

/* TODO A2: `size -B` 출력 "text data bss dec hex filename" 의 숫자 줄을 파싱. 성공 true. */
static bool parse_size_B(const char *txt, berk_t *b) {
    (void)txt; (void)b;
    return false;                       /* TODO A2 */
}

/* TODO A3: map 에서 심볼 값 찾기. 두 형태 모두:
 *   "                0x00001000                        _Min_Stack_Size = 0x1000"   (스크립트 대입)
 *   "                0x08000180                sine_lut"                           (입력 섹션 안의 심볼)
 * 첫 토큰이 0x 주소, 둘째 토큰이 정확히 sym 이면 그 주소. 없으면 false. */
static bool map_symbol(const char *map, const char *sym, uint32_t *val) {
    (void)map; (void)sym; (void)val;
    return false;                       /* TODO A3 */
}

static bool in_region(uint32_t a, uint32_t org, uint32_t len) { return a >= org && a - org < len; }
static bool has_prefix(const char *s, const char *pre) { return strncmp(s, pre, strlen(pre)) == 0; }

/* TODO A4: 예산표. 분류 규칙 (이 레슨의 규칙 — 표로 md 5. 풀이에 있음):
 *   addr == 0                      → 비할당(.debug_*, .comment, .ARM.attributes): 어디에도 안 셈
 *   Flash 영역 안: ".rodata" 접두 → rodata, 나머지(.isr_vector, .text ...) → text
 *   RAM 영역 안:   ".bss"/".noinit" 접두 → bss (NOBITS: Flash 0)
 *                  "._user_heap_stack" → heap + stack + hs_pad  (hs_pad = 크기 - heap - stack)
 *                  그 밖(.data 등, 내용이 있는 섹션) → data: RAM 과 Flash(LMA) 양쪽
 *   어느 영역도 아님 → 분류 실패 (반환값으로 개수 보고 — 조용히 버리지 말 것)
 * flash_total = text + rodata + data, ram_total = data + bss + heap + stack + hs_pad,
 * pct_x100 = total * 10000 / 영역 크기 (64비트로, 버림). */
static int budget_build(const sec_t *s, int n, const part_t *pt, uint32_t heap, uint32_t stack, budget_t *b) {
    (void)s; (void)n; (void)pt; (void)heap; (void)stack;
    memset(b, 0, sizeof *b);            /* TODO A4 */
    return 0;
}

/* ======================= Part B: map 입력 섹션 → 두 빌드 diff ======================= */

/* TODO B1: map 의 "Linker script and memory map" 이후에서 입력 섹션 줄을 읽는다.
 *   " .data.g_cfg    0x20000014        0x8 fw_good.o"          ← 한 줄
 *   " .rodata.sine_lut"                                        ← 이름이 길면 줄바꿈되고
 *   "                0x08000180     0x2000 fw_good.o"          ← 다음 줄에 주소·크기
 * 규칙: 줄이 " ." 로 시작 (출력 섹션은 0열 ".", 패턴 " *(", 채움 " *fill*" 은 제외). 크기 0 은 건너뜀.
 * kind = 두 번째 '.' 앞까지 (".rodata.sine_lut" → ".rodata"), sym = 그 뒤 ("sine_lut");
 * 두 번째 '.' 이 없으면 kind = sym = 이름 전체 (".isr_vector"). 반환: 개수, cap 초과 -1. */
static int parse_map_inputs(const char *map, isec_t *out, int cap) {
    (void)map; (void)out; (void)cap;
    return 0;                           /* TODO B1 */
}

static bool kind_in_ram(const char *k) { return strcmp(k, ".data") == 0 || strcmp(k, ".bss") == 0; }

/* TODO B2: 두 빌드에서 같은 sym 의 kind 가 바뀐 것을 모두 찾는다 (예: sine_lut .rodata → .data).
 * size = after 쪽 크기. ram_delta: Flash 전용(.text/.rodata) → RAM(.data/.bss) 이면 +size, 반대면 -size,
 * 그 외 0. 결과를 size 내림차순으로 정렬. 반환: 개수 (cap 초과분은 버리고 cap). */
static int diff_moves(const isec_t *before, int nb, const isec_t *after, int na, move_t *out, int cap) {
    (void)before; (void)nb; (void)after; (void)na; (void)out; (void)cap;
    return 0;                           /* TODO B2 */
}

/* TODO B3: 한 빌드만 보고 거는 린트: kind == ".data" 이고 size >= threshold 인 입력 섹션의 인덱스를
 * hits[] 에 모은다 ("초기값 있는 큰 RAM 객체 = const 누락 의심"). 반환: 개수 (최대 cap). */
static int lint_large_rw(const isec_t *s, int n, uint32_t threshold, int *hits, int cap) {
    (void)s; (void)n; (void)threshold; (void)hits; (void)cap;
    return 0;                           /* TODO B3 */
}

/* ======================= Part C: CI 예산 게이트 ======================= */

/* TODO C1: 사용량을 부품 크기와 비교. used > len → *_OVER (over_by = used - len, 아니면 0),
 * 아니고 used * 100 > warn_pct * len → *_WARN. 반환: GATE_* 비트 OR. (64비트 곱으로 오버플로 방지) */
static unsigned budget_gate(uint32_t flash_used, uint32_t ram_used, const part_t *pt, unsigned warn_pct,
                            uint32_t *flash_over_by, uint32_t *ram_over_by) {
    (void)flash_used; (void)ram_used; (void)pt; (void)warn_pct;
    *flash_over_by = 0u; *ram_over_by = 0u;
    return 0u;                          /* TODO C1 */
}

/* ======================= 하네스 ======================= */
static const part_t F411RE = { "STM32F411RE", 0x08000000u, 512u * 1024u, 0x20000000u, 128u * 1024u };
static const part_t F103C8 = { "STM32F103C8", 0x08000000u, 64u * 1024u, 0x20000000u, 20u * 1024u };
static const part_t L031K6 = { "STM32L031K6", 0x08000000u, 32u * 1024u, 0x20000000u, 8u * 1024u };

static void print_budget(const char *tag, const budget_t *b, const part_t *pt) {
    printf("--- %s budget on %s (Flash %u KB / RAM %u KB) ---\n", tag, pt->name,
           (unsigned)(pt->flash_len / 1024u), (unsigned)(pt->ram_len / 1024u));
    printf("  %-24s %7s %7s\n", "item", "Flash", "RAM");
    printf("  %-24s %7u %7s\n", ".isr_vector + .text", (unsigned)b->text, "-");
    printf("  %-24s %7u %7s\n", ".rodata", (unsigned)b->rodata, "-");
    printf("  %-24s %7u %7u\n", ".data (LMA + VMA)", (unsigned)b->data, (unsigned)b->data);
    printf("  %-24s %7s %7u\n", ".bss", "-", (unsigned)b->bss);
    printf("  %-24s %7s %7u\n", "heap (_Min_Heap_Size)", "-", (unsigned)b->heap);
    printf("  %-24s %7s %7u\n", "stack (_Min_Stack_Size)", "-", (unsigned)b->stack);
    printf("  %-24s %7s %7u\n", "align pad", "-", (unsigned)b->hs_pad);
    printf("  %-24s %7u %7u\n", "total", (unsigned)b->flash_total, (unsigned)b->ram_total);
    printf("  %-24s %3u.%02u%% %3u.%02u%%\n", "used", (unsigned)(b->flash_pct_x100 / 100u),
           (unsigned)(b->flash_pct_x100 % 100u), (unsigned)(b->ram_pct_x100 / 100u),
           (unsigned)(b->ram_pct_x100 % 100u));
}

static const sec_t *find_sec(const sec_t *s, int n, const char *name) {
    for (int i = 0; i < n; i++) if (strcmp(s[i].name, name) == 0) return &s[i];
    return NULL;
}
static const isec_t *find_in(const isec_t *s, int n, const char *sym) {
    for (int i = 0; i < n; i++) if (strcmp(s[i].sym, sym) == 0) return &s[i];
    return NULL;
}

int main(void) {
    if (!read_file("fw_good.map", g_map_good, sizeof g_map_good) ||
        !read_file("fw_bad.map", g_map_bad, sizeof g_map_bad) ||
        !read_file("size_A_good.txt", g_sa_good, sizeof g_sa_good) ||
        !read_file("size_A_bad.txt", g_sa_bad, sizeof g_sa_bad) ||
        !read_file("size_B_good.txt", g_sb_good, sizeof g_sb_good) ||
        !read_file("size_B_bad.txt", g_sb_bad, sizeof g_sb_bad) ||
        !read_file("ldmem_good.txt", g_ld_good, sizeof g_ld_good) ||
        !read_file("ldmem_bad.txt", g_ld_bad, sizeof g_ld_bad) ||
        !read_file("ldmem_ovf.txt", g_ld_ovf, sizeof g_ld_ovf)) return 1;

    /* ---- Part A ---- */
    CHECK(in_region(0x0807FFFFu, F411RE.flash_org, F411RE.flash_len) && !in_region(0x08080000u, F411RE.flash_org,
          F411RE.flash_len) && !in_region(0x07FFFFFFu, F411RE.flash_org, F411RE.flash_len));  /* 주어진 도우미 */
    CHECK(has_prefix(".rodata.str1.4", ".rodata") && !has_prefix(".rod", ".rodata") && kind_in_ram(".bss"));
    static sec_t sg[32], sb[32];
    int ng = parse_size_A(g_sa_good, sg, 32), nbd = parse_size_A(g_sa_bad, sb, 32);
    CHECK(ng == 16 && nbd == 16);
    CHECK(ng > 0 && strcmp(sg[0].name, ".isr_vector") == 0 && sg[0].size == 0x40u && sg[0].addr == 0x08000000u);
    const sec_t *ro = ng > 0 ? find_sec(sg, ng, ".rodata") : NULL, *hs = ng > 0 ? find_sec(sg, ng, "._user_heap_stack") : NULL;
    CHECK(ro && ro->size == 8240u && ro->addr == 0x08000150u);
    CHECK(hs && hs->size == 0x1804u && hs->addr == 0x20000c24u);
    CHECK(parse_size_A("fw.elf  :\nsection size addr\n.text 0x10 0x8000000\n", sg + 20, 4) == -1); /* Total 없음 */
    CHECK(parse_size_A(g_sa_good, sg + 20, 3) == -1);                                              /* cap 초과 */

    berk_t bg = {0}, bbd = {0};
    CHECK(parse_size_B(g_sb_good, &bg) && bg.text == 8576u && bg.data == 28u && bg.bss == 9228u && bg.dec == 17832u);
    CHECK(parse_size_B(g_sb_bad, &bbd) && bbd.text == 364u && bbd.data == 8240u && bbd.bss == 9224u);
    CHECK(!parse_size_B("   text\t   data\t    bss\n", &bbd));

    uint32_t heap = 0, stack = 0, v = 0;
    CHECK(map_symbol(g_map_good, "_Min_Heap_Size", &heap) && heap == 0x800u);
    CHECK(map_symbol(g_map_good, "_Min_Stack_Size", &stack) && stack == 0x1000u);
    CHECK(map_symbol(g_map_good, "_estack", &v) && v == 0x20020000u);
    CHECK(map_symbol(g_map_good, "sine_lut", &v) && v == 0x08000180u);   /* good: Flash */
    CHECK(map_symbol(g_map_bad, "sine_lut", &v) && v == 0x20000030u);    /* bad: RAM */
    CHECK(map_symbol(g_map_bad, "_sidata", &v) && v == 0x0800016cu);
    CHECK(!map_symbol(g_map_good, "sine", &v));                          /* 접두어만 같으면 안 됨 */

    budget_t Bg, Bb; uint32_t ldf = 0, ldr = 0;
    CHECK(budget_build(sg, ng > 0 ? ng : 0, &F411RE, heap, stack, &Bg) == 0);
    CHECK(budget_build(sb, nbd > 0 ? nbd : 0, &F411RE, heap, stack, &Bb) == 0);
    CHECK(Bg.text == 336u && Bg.rodata == 8240u && Bg.data == 28u && Bg.bss == 3080u);
    CHECK(Bg.heap == 2048u && Bg.stack == 4096u && Bg.hs_pad == 4u && Bb.hs_pad == 0u);
    CHECK(parse_ld_mem(g_ld_good, "FLASH", &ldf) && Bg.flash_total == ldf);       /* ld 가 직접 센 값과 일치 */
    CHECK(parse_ld_mem(g_ld_good, "RAM", &ldr) && Bg.ram_total == ldr);
    CHECK(parse_ld_mem(g_ld_bad, "FLASH", &ldf) && Bb.flash_total == ldf && Bb.flash_total == Bg.flash_total);
    CHECK(parse_ld_mem(g_ld_bad, "RAM", &ldr) && Bb.ram_total == ldr && ldr == 17464u);
    CHECK(Bg.flash_pct_x100 == 164u && Bg.ram_pct_x100 == 706u && Bb.ram_pct_x100 == 1332u);
    CHECK(Bg.flash_total == bg.text + bg.data && Bg.ram_total == bg.data + bg.bss);     /* size -B 교차 검증 */
    CHECK(Bb.flash_total == bbd.text + bbd.data && Bb.ram_total == bbd.data + bbd.bss);
    sec_t ccm[3] = { { ".text", 0x100u, 0x08000000u }, { ".ccmram", 0x400u, 0x10000000u }, { ".debug_line", 0x50u, 0u } };
    budget_t Bc;
    CHECK(budget_build(ccm, 3, &F411RE, 0u, 0u, &Bc) == 1 && Bc.flash_total == 0x100u);   /* CCM 은 보고 */

    /* ---- Part B ---- */
    static isec_t ig[64], ib[64];
    int mg = parse_map_inputs(g_map_good, ig, 64), mb = parse_map_inputs(g_map_bad, ib, 64);
    CHECK(mg == 14 && mb == 14);
    const isec_t *x = mg > 0 ? find_in(ig, mg, "sine_lut") : NULL;
    CHECK(x && strcmp(x->kind, ".rodata") == 0 && x->addr == 0x08000180u && x->size == 0x2000u); /* 줄바꿈형 */
    x = mg > 0 ? find_in(ig, mg, "g_cfg") : NULL;
    CHECK(x && strcmp(x->kind, ".data") == 0 && x->addr == 0x20000014u && x->size == 8u);        /* 한 줄형 */
    x = mg > 0 ? find_in(ig, mg, "startup.main") : NULL;
    CHECK(x && strcmp(x->kind, ".text") == 0 && x->size == 0x9cu);
    x = mg > 0 ? find_in(ig, mg, ".isr_vector") : NULL;
    CHECK(x && strcmp(x->kind, ".isr_vector") == 0 && x->size == 0x40u);
    x = mb > 0 ? find_in(ib, mb, "sine_lut") : NULL;
    CHECK(x && strcmp(x->kind, ".data") == 0 && x->addr == 0x20000030u);
    CHECK(mg > 0 && find_in(ig, mg, "glue_7") == NULL && find_in(ig, mg, "*fill*") == NULL);    /* 0 크기·채움 */
    CHECK(parse_map_inputs("Linker script and memory map\n .data.cut\n", ig + 60, 4) == 0);      /* 끝에서 잘림 */
    CHECK(parse_map_inputs(g_map_good, ig + 60, 4) == -1);

    move_t mv[8];
    int nm = diff_moves(ig, mg > 0 ? mg : 0, ib, mb > 0 ? mb : 0, mv, 8);
    CHECK(nm == 2);
    CHECK(nm >= 1 && strcmp(mv[0].sym, "sine_lut") == 0 && strcmp(mv[0].from, ".rodata") == 0 &&
          strcmp(mv[0].to, ".data") == 0 && mv[0].size == 8192u && mv[0].ram_delta == 8192);
    CHECK(nm >= 2 && strcmp(mv[1].sym, "cmd_names") == 0 && mv[1].ram_delta == 16);
    CHECK(nm >= 2 && (uint32_t)(mv[0].ram_delta + mv[1].ram_delta) == Bb.ram_total - Bg.ram_total);
    int back = diff_moves(ib, mb > 0 ? mb : 0, ig, mg > 0 ? mg : 0, mv + 4, 4);              /* 고친 방향 */
    CHECK(back == 2 && mv[4].ram_delta == -8192);
    CHECK(diff_moves(ig, mg > 0 ? mg : 0, ig, mg > 0 ? mg : 0, mv, 8) == 0);                   /* 자기 자신 */

    int hits[8];
    int lb = lint_large_rw(ib, mb > 0 ? mb : 0, 256u, hits, 8);
    CHECK(lb == 1 && strcmp(ib[hits[0]].sym, "sine_lut") == 0);
    CHECK(lint_large_rw(ig, mg > 0 ? mg : 0, 256u, hits, 8) == 0);
    CHECK(lint_large_rw(ib, mb > 0 ? mb : 0, 16u, hits, 8) == 3);         /* 임계 16: cmd_names + g_fw_name(오탐) */

    /* ---- Part C ---- */
    uint32_t fo = 0, rov = 0;
    CHECK(budget_gate(Bg.flash_total, Bg.ram_total, &F411RE, 80u, &fo, &rov) == 0u);
    CHECK(budget_gate(Bb.flash_total, Bb.ram_total, &F411RE, 80u, &fo, &rov) == 0u);
    CHECK(budget_gate(Bg.flash_total, Bg.ram_total, &F103C8, 80u, &fo, &rov) == 0u);              /* 45.19 % */
    CHECK(budget_gate(Bb.flash_total, Bb.ram_total, &F103C8, 80u, &fo, &rov) == GATE_RAM_WARN);   /* 85.27 % */
    CHECK(budget_gate(Bg.flash_total, Bg.ram_total, &L031K6, 80u, &fo, &rov) == GATE_RAM_OVER && rov == 1064u);
    CHECK(budget_gate(L031K6.flash_len, 0u, &L031K6, 100u, &fo, &rov) == 0u && fo == 0u);         /* 정확히 꽉 참 */
    CHECK(budget_gate(L031K6.flash_len + 1u, 0u, &L031K6, 100u, &fo, &rov) == GATE_FLASH_OVER && fo == 1u);
    uint32_t ovf_used = 0, ovf_by = 0;
    CHECK(parse_ld_mem(g_ld_ovf, "RAM", &ovf_used) && parse_ld_overflow(g_ld_ovf, "RAM", &ovf_by));
    CHECK(budget_gate(Bg.flash_total, ovf_used, &F411RE, 80u, &fo, &rov) == GATE_RAM_OVER && rov == ovf_by && rov == 40u);
    CHECK(Bg.ram_total - 1024u + 122880u == ovf_used);   /* 링크 전에 예측: rx_buf 1 KB → 120 KB */

    print_budget("good", &Bg, &F411RE);
    print_budget("bad (const missing)", &Bb, &F411RE);
    printf("--- diff good → bad (moved input sections) ---\n");
    for (int i = 0; i < nm && i < 8; i++)
        printf("  %-10s %-8s → %-6s %6u B  RAM %+d\n", mv[i].sym, mv[i].from, mv[i].to,
               (unsigned)mv[i].size, (int)mv[i].ram_delta);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
